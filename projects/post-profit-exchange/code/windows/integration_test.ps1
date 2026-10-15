# SPDX-License-Identifier: MIT
# FUTURE acceptance test: requires the actual supplied Windows IdyDB library.
# Never points at, opens, or migrates the live vendor environment.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Build,
    [Parameter(Mandatory=$true)][string]$IdydbPackage,
    [Parameter(Mandatory=$true)][string]$Page,
    [Parameter(Mandatory=$true)][string]$Receipt
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$buildRoot = (Resolve-Path -LiteralPath $Build).Path
$packageRoot = (Resolve-Path -LiteralPath $IdydbPackage).Path
$pagePath = (Resolve-Path -LiteralPath $Page).Path
$server = Join-Path $buildRoot 'post-profit-exchange.exe'
$cli = Join-Path $buildRoot 'exchange_environment.exe'
$control = Join-Path $buildRoot 'exchange_windows_server_platform_test.exe'
$security = Join-Path $buildRoot 'exchange_windows_storage_files_test.exe'
$storage = Join-Path $buildRoot 'exchange_windows_database_storage_test.exe'
$dependency = Join-Path $packageRoot 'exchange-idydb-windows.json'
foreach ($required in @($server, $cli, $control, $security, $storage, $dependency, $pagePath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required test input missing: $required" }
}
$receiptPath = [IO.Path]::GetFullPath($Receipt)
if (Test-Path -LiteralPath $receiptPath) { throw 'Receipt already exists; choose a new evidence path.' }
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('exchange-windows-acceptance-' + [Guid]::NewGuid().ToString('N'))
$drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($testRoot))
if ($drive.DriveType -ne 'Fixed' -or $drive.DriveFormat -ne 'NTFS') {
    throw 'This acceptance test requires a fixed local NTFS volume.'
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
$acl = New-Object Security.AccessControl.DirectorySecurity
$acl.SetOwner($identity)
$acl.SetAccessRuleProtection($true, $false)
$rule = New-Object Security.AccessControl.FileSystemAccessRule(
    $identity, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
$acl.AddAccessRule($rule)
New-Item -ItemType Directory -Path $testRoot | Out-Null
Set-Acl -LiteralPath $testRoot -AclObject $acl
$environmentRoot = Join-Path $testRoot 'operation with spaces'
$keyDirectory = Join-Path $testRoot 'keys'
foreach ($directory in @($environmentRoot, $keyDirectory, (Join-Path $environmentRoot 'records'))) {
    New-Item -ItemType Directory -Path $directory | Out-Null
}
$utf8 = New-Object Text.UTF8Encoding($false)
$keyPath = Join-Path $keyDirectory 'operation.key'
$wrongKeyPath = Join-Path $keyDirectory 'wrong.key'
# Deterministic credentials are isolated test data, never vendor credentials.
[IO.File]::WriteAllText($keyPath, ('0123456789abcdef' * 4), $utf8)
[IO.File]::WriteAllText($wrongKeyPath, ('fedcba9876543210' * 4), $utf8)
function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), $utf8)
}
function Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Invoke-Accounts([string]$Command, [string]$Key = $keyPath, $Body = $null, [bool]$Reject = $false) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $cli
    $start.Arguments = (@($Command, $environmentRoot, '--key-file', $Key) | ForEach-Object { Quote-Path $_ }) -join ' '
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $commandProcess = [Diagnostics.Process]::Start($start)
    try {
        $stdoutTask = $commandProcess.StandardOutput.ReadToEndAsync()
        $stderrTask = $commandProcess.StandardError.ReadToEndAsync()
        if ($null -ne $Body) { $commandProcess.StandardInput.WriteLine(($Body | ConvertTo-Json -Depth 32 -Compress)) }
        $commandProcess.StandardInput.Close()
        Check ($commandProcess.WaitForExit(15000)) ('Command timed out; inspect isolated PID ' + $commandProcess.Id)
        $text = $stdoutTask.GetAwaiter().GetResult()
        $diagnostics = $stderrTask.GetAwaiter().GetResult()
        foreach ($output in @($text, $diagnostics)) {
            Check (-not $output.Contains('0123456789abcdef' * 4)) 'A diagnostic exposed the test key.'
            Check (-not $output.Contains('fedcba9876543210' * 4)) 'A diagnostic exposed the wrong test key.'
        }
        $diagnosticPath = Join-Path $testRoot ('cli-' + [Guid]::NewGuid().ToString('N') + '.stderr')
        [IO.File]::WriteAllText($diagnosticPath, $diagnostics, $utf8)
        Check (($commandProcess.ExitCode -ne 0) -eq $Reject) ('Unexpected command outcome: ' + $Command)
        $value = $text | ConvertFrom-Json
        if ($Reject) { Check ($value.status -eq 'error') 'Rejected operation lacks error status.' }
        return $value
    } finally { $commandProcess.Dispose() }
}
function Event([string]$Id, [int]$Revision, [string]$Kind, $Details) {
    return @{ expected_revision=$Revision; event=@{
        schema_version='exchange.transaction.v1'; environment_id='windows-test';
        id=$Id; date='2026-10-15'; kind=$Kind; details=$Details
    }}
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Quote-Path([string]$Value) {
    if ($Value.Contains('"') -or $Value.Contains([string][char]10) -or $Value.EndsWith('\')) { throw 'Unsupported test command argument.' }
    return '"' + $Value + '"'
}
# Match the exact runtime DLLs that Windows will load beside the tested EXEs.
$provider = Get-Content -LiteralPath $dependency -Raw | ConvertFrom-Json
$runtimeDigests = [ordered]@{}
foreach ($item in $provider.runtime_dlls) {
    Check ($item.path -match '^[A-Za-z0-9_.+/-]+$' -and
        -not ($item.path.Split('/') -contains '..') -and
        -not [IO.Path]::IsPathRooted($item.path)) 'Invalid provider runtime path.'
    $name = [IO.Path]::GetFileName($item.path).ToLowerInvariant()
    Check (-not $runtimeDigests.Contains($name)) 'Duplicate provider runtime DLL.'
    $supplied = Join-Path $packageRoot $item.path
    $loaded = Join-Path $buildRoot $name
    Check ((Hash $supplied) -eq $item.sha256 -and (Hash $loaded) -eq $item.sha256) 'Runtime DLL differs from the supplied package.'
    $runtimeDigests[$name] = $item.sha256
}
$adjacentDlls = @(Get-ChildItem -LiteralPath $buildRoot -File -Filter '*.dll')
Check ($adjacentDlls.Count -eq $runtimeDigests.Count) 'Unlisted DLLs are present beside the test applications.'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$listener.Start()
$port = $listener.LocalEndpoint.Port
$listener.Stop()
$config = @{
    schema_version='exchange.environment.v2'; environment_id='windows-test';
    currency='EUR'; minor_unit_digits=2; storage=@{database='data/exchange.idydb'};
    metadata_file='metadata.json'; opening_file='records/opening.json';
    runtime=@{workspace='.';server=@{bind='127.0.0.1';port=$port};
        encryption=@{required=$true;key_file=$keyPath}}
}
Write-Json (Join-Path $environmentRoot 'exchange.cfg') $config
Write-Json (Join-Path $environmentRoot 'metadata.json') @{
    schema_version='exchange.environment-metadata.v1'; display_name='Windows integration fixture';
    description='Synthetic accounts used only for deterministic application acceptance.';
    provenance='synthetic'; products=@(@{sku='bread';label='Bread';unit='piece'})
}
Write-Json (Join-Path $environmentRoot 'records/opening.json') @{
    schema_version='exchange.opening.v1';environment_id='windows-test';as_of='2026-10-14';
    cash=100000;reserve=5000;earned_funding=0;
    obligations=@(@{id='opening-wages';kind='wages';amount=800},@{id='opening-operating';kind='operating';amount=200});
    products=@(@{sku='bread';published_price=200;replacement_cost=100;
        lots=@(@{id='opening-bread';units=45;unit_cost=100})})
}
$checks = [ordered]@{}
$process = $null
try {
    $securityOutput = & $security 2>&1 | Out-String
    Check ($LASTEXITCODE -eq 0 -and -not $securityOutput.Contains('SKIP:')) 'Native filesystem security acceptance did not fully pass.'
    $checks.windows_storage_security = $true
    $storageOutput = & $storage 2>&1 | Out-String
    Check ($LASTEXITCODE -eq 0 -and -not $storageOutput.Contains('SKIP:')) 'Encrypted storage acceptance did not fully pass.'
    $checks.encrypted_storage_contract = $true
    $opening = Invoke-Accounts 'initialize'
    Check ($opening.revision -eq 0 -and $opening.state.cash -eq 100000) 'Opening accounts differ.'
    $delivery = Event 'delivery-1' 0 'delivery' @{sku='bread';lot_id='received-bread';units=10;unit_cost=120}
    $sale = Event 'sale-1' 1 'sale' @{sku='bread';units=50;unit_price=200}
    $expense = Event 'expense-1' 2 'expense' @{category='operating';description='Electricity fixture';amount=700;settlement='paid'}
    foreach ($event in @($delivery,$sale,$expense)) { $result = Invoke-Accounts 'transact' -Body $event }
    Check ($result.revision -eq 3 -and $result.state.cash -eq 108100 -and
        $result.state.earned_funding -eq 4200 -and $result.summary.inventory_value -eq 600 -and
        $result.summary.inventory_units -eq 5 -and $result.summary.net_assets -eq 107700) 'Resulting accounts differ.'
    $checks.encrypted_roundtrip = $true
    $reopened = Invoke-Accounts 'inspect'
    foreach ($field in @('state','summary','revision','events')) {
        Check (($reopened.$field | ConvertTo-Json -Depth 32 -Compress) -eq
            ($result.$field | ConvertTo-Json -Depth 32 -Compress)) ('Reopened field differs: ' + $field)
    }
    $checks.reopen = $true
    $database = Join-Path $environmentRoot 'data/exchange.idydb'
    $before = Hash $database
    $null = Invoke-Accounts 'inspect' -Key $wrongKeyPath -Reject $true
    $null = Invoke-Accounts 'transact' -Key $wrongKeyPath -Body (Event 'wrong-key' 3 'sale' @{sku='bread';units=1;unit_price=200}) -Reject $true
    Check ((Hash $database) -eq $before) 'Wrong-key access changed ciphertext.'
    $checks.wrong_key_preserves_ciphertext = $true
    $bytes = [IO.File]::ReadAllBytes($database)
    $ciphertext = [Text.Encoding]::ASCII.GetString($bytes)
    Check ($ciphertext.StartsWith('IDYDBENC') -and -not $ciphertext.Contains('Electricity fixture') -and
        -not $ciphertext.Contains('"cash"')) 'Database is not the expected encrypted format.'
    $checks.ciphertext_only = $true
    $retry = Invoke-Accounts 'transact' -Body $expense
    Check ($retry.revision -eq 3 -and $retry.state.cash -eq 108100 -and (Hash $database) -eq $before) 'Retry duplicated a record.'
    $checks.idempotent_retry = $true
    $null = Invoke-Accounts 'transact' -Body (Event 'stale' 0 'sale' @{sku='bread';units=1;unit_price=200}) -Reject $true
    Check ((Hash $database) -eq $before) 'Stale revision changed ciphertext.'
    $checks.stale_revision = $true
    $stdout = Join-Path $testRoot 'server.stdout'
    $stderr = Join-Path $testRoot 'server.stderr'
    $arguments = @('--config', (Quote-Path (Join-Path $environmentRoot 'exchange.cfg')), '--no-browser', '--page', (Quote-Path $pagePath))
    $process = Start-Process -FilePath $server -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $url = 'http://127.0.0.1:' + $port
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $session = $null
    do {
        $process.Refresh()
        if ($process.HasExited) { throw 'Server exited before readiness.' }
        try { $session = Invoke-RestMethod -Uri ($url + '/api/session') -TimeoutSec 1; break }
        catch { Start-Sleep -Milliseconds 100 }
    } while ([DateTime]::UtcNow -lt $deadline)
    Check ($null -ne $session -and $session.token.Length -eq 64) 'Server did not become ready.'
    $state = Invoke-RestMethod -Uri ($url + '/api/state') -TimeoutSec 5
    Check ($state.revision -eq 3 -and $state.state.cash -eq 108100) 'HTTP accounts differ.'
    $html = Invoke-WebRequest -Uri ($url + '/') -UseBasicParsing -TimeoutSec 5
    Check ($html.StatusCode -eq 200 -and $html.Content.Contains('<html')) 'Existing browser interface unavailable.'
    Check (-not ($state | ConvertTo-Json -Depth 32).Contains($keyPath)) 'Runtime key path leaked into report.'
    $checks.http_start_and_accounts = $true
    $httpEvent = Event 'http-sale' 3 'sale' @{sku='bread';units=1;unit_price=200}
    $headers = @{'X-Exchange-Token'=$session.token;Origin=$url}
    $httpBody = $httpEvent | ConvertTo-Json -Depth 32 -Compress
    $posted = Invoke-RestMethod -Uri ($url + '/api/transactions') -Method Post -Headers $headers -ContentType 'application/json' -Body $httpBody -TimeoutSec 10
    Check ($posted.revision -eq 4 -and $posted.state.cash -eq 108300 -and
        $posted.state.earned_funding -eq 4280 -and $posted.summary.inventory_units -eq 4 -and
        $posted.summary.inventory_value -eq 480 -and $posted.summary.net_assets -eq 107780) 'HTTP transaction accounts differ.'
    $httpHash = Hash $database
    $httpRetry = Invoke-RestMethod -Uri ($url + '/api/transactions') -Method Post -Headers $headers -ContentType 'application/json' -Body $httpBody -TimeoutSec 10
    Check ($httpRetry.revision -eq 4 -and $httpRetry.state.cash -eq 108300 -and
        (Hash $database) -eq $httpHash) 'HTTP retry duplicated a transaction.'
    $checks.http_transaction_and_retry = $true
    & $control --stop-server $process.Id
    Check ($LASTEXITCODE -eq 0) 'Control helper could not stop the isolated server.'
    Check ($process.WaitForExit(5000)) 'Server did not stop gracefully.'
    $process.Refresh()
    Check ($process.ExitCode -eq 0) 'Server reported unsuccessful shutdown.'
    $checks.graceful_shutdown = $true
    $after = Invoke-Accounts 'inspect'
    Check ($after.revision -eq 4 -and $after.state.cash -eq 108300 -and
        $after.state.earned_funding -eq 4280 -and $after.summary.inventory_value -eq 480 -and
        $after.summary.inventory_units -eq 4 -and $after.summary.net_assets -eq 107780) 'Reopened HTTP transaction accounts differ.'
    $receiptValue = @{
        schema_version='exchange.windows-integration.v1';platform='windows-ntfs';
        server_sha256=(Hash $server);cli_sha256=(Hash $cli);
        security_test_sha256=(Hash $security);storage_test_sha256=(Hash $storage);
        idydb_manifest_sha256=(Hash $dependency);page_sha256=(Hash $pagePath);
        runtime_dlls=$runtimeDigests;tests=$checks
    }
    Write-Json $receiptPath $receiptValue
    Write-Output ('Windows application integration passed. Receipt: ' + $receiptPath)
} finally {
    if ($null -ne $process) {
        $process.Refresh()
        if (-not $process.HasExited) {
            & $control --stop-server $process.Id
            if ($LASTEXITCODE -ne 0) {
                Write-Warning ('Test server remains active; stop only PID ' + $process.Id + '. Fixture preserved: ' + $testRoot)
            }
        }
    }
    # Preserve the isolated fixture and diagnostics for inspection. Never delete
    # arbitrary caller paths and never write a success receipt after a failure.
    Write-Output ('Synthetic test fixture: ' + $testRoot)
}
