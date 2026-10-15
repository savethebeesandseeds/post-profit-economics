# SPDX-License-Identifier: MIT
# One application container per operating environment; shared development is separate.
param(
    [ValidateSet('plan', 'provision', 'up', 'status', 'stop', 'logs')]
    [string]$Action = 'plan',
    [string]$Config = 'projects/post-profit-exchange/environments/vendor_0x01/exchange.cfg',
    [switch]$OpenBrowser
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..')).TrimEnd('\', '/')
$rootItem = Get-Item -LiteralPath $projectRoot -Force
if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Use the real checkout directory; nothing changed.'
}
function Assert-OrdinaryDirectoryChain {
    param([string]$Path)
    $cursor = Get-Item -LiteralPath $Path -Force
    while ($null -ne $cursor) {
        if (-not ($cursor.Attributes -band [IO.FileAttributes]::Directory) -or ($cursor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Directory paths must exist and cannot use reparse points.'
        }
        $cursor = $cursor.Parent
    }
}
$configPath = if ([IO.Path]::IsPathRooted($Config)) {
    [IO.Path]::GetFullPath($Config)
} else { [IO.Path]::GetFullPath((Join-Path $projectRoot $Config)) }
$configItem = Get-Item -LiteralPath $configPath -Force
if ($configItem.PSIsContainer -or ($configItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
    $configItem.Length -gt 1048576 -or $configItem.Name -cne 'exchange.cfg') {
    throw 'Configuration must be an ordinary exchange.cfg file no larger than 1 MiB.'
}
$configText = Get-Content -LiteralPath $configPath -Raw
$environmentConfig = $configText | ConvertFrom-Json
foreach ($name in @('schema_version', 'environment_id', 'runtime', 'workspace', 'server', 'bind', 'port', 'encryption', 'required', 'key_file')) {
    if ([regex]::Matches($configText, '"' + $name + '"\s*:').Count -ne 1) {
        throw "Configuration must contain exactly one '$name' property."
    }
}
if ($environmentConfig.schema_version -cne 'exchange.environment.v2' -or $environmentConfig.environment_id -isnot [string] -or
    $environmentConfig.environment_id.Length -gt 48 -or $environmentConfig.environment_id -cnotmatch '^[a-z0-9]+([_-][a-z0-9]+)*$') {
    throw 'Environment id must be lowercase letters/digits separated by underscores or hyphens, at most 48 characters.'
}
$runtime = $environmentConfig.runtime
if ($runtime.workspace -isnot [string] -or [string]::IsNullOrWhiteSpace($runtime.workspace)) {
    throw 'runtime.workspace must identify the environment directory, relative to exchange.cfg or absolute.'
}
$environmentHost = if ([IO.Path]::IsPathRooted($runtime.workspace)) {
    [IO.Path]::GetFullPath($runtime.workspace).TrimEnd('\', '/')
} else { [IO.Path]::GetFullPath((Join-Path $configItem.DirectoryName $runtime.workspace)).TrimEnd('\', '/') }
$environmentParent = Join-Path $projectRoot 'projects/post-profit-exchange/environments'
$environmentId = $environmentConfig.environment_id
if ([IO.Path]::GetDirectoryName($environmentHost) -ine $environmentParent -or
    [IO.Path]::GetFileName($environmentHost) -cne $environmentId -or
    $configPath -ine (Join-Path $environmentHost 'exchange.cfg')) {
    throw 'The configuration must belong to its named environment directly under projects/post-profit-exchange/environments.'
}
Assert-OrdinaryDirectoryChain $environmentHost
$environmentPath = 'projects/post-profit-exchange/environments/' + $environmentId
if ($runtime.server.bind -cne '127.0.0.1' -or
    $runtime.server.port -isnot [ValueType] -or $runtime.server.port -is [bool] -or
    $runtime.server.port -lt 1024 -or $runtime.server.port -gt 65535 -or
    [double]$runtime.server.port -ne [math]::Truncate([double]$runtime.server.port)) {
    throw 'runtime.server requires bind 127.0.0.1 and an integer port from 1024 to 65535.'
}
$bindAddress = $runtime.server.bind
$Port = [int]$runtime.server.port
if ($runtime.encryption.required -isnot [bool] -or -not $runtime.encryption.required -or
    $runtime.encryption.key_file -isnot [string]) {
    throw 'runtime.encryption requires required=true and a Windows key_file path.'
}
$keyPath = $runtime.encryption.key_file.Replace('%USERPROFILE%', [Environment]::GetFolderPath('UserProfile'))
if ($keyPath.Contains('%') -or $keyPath -cnotmatch '^[A-Za-z]:[\\/]') {
    throw 'key_file must be absolute or begin with %USERPROFILE%; other variable expansions are not supported.'
}
$keyFile = [IO.Path]::GetFullPath($keyPath)
if ($keyFile -ieq $projectRoot -or $keyFile.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $keyFile.Contains(',')) {
    throw 'Keep the key file outside the checkout and environment. Commas are not supported in Docker bind paths.'
}
if ($projectRoot.Contains(',') -or $environmentHost.Contains(',')) {
    throw 'Commas are not supported in Docker bind paths.'
}
# Status, stop and logs remain available if the key has been lost or disconnected.
if ($Action -in @('plan', 'provision', 'up')) {
    $keyItem = Get-Item -LiteralPath $keyFile -Force
    if ($keyItem.PSIsContainer -or ($keyItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $keyItem.Length -lt 64 -or $keyItem.Length -gt 66) {
        throw 'The key must be an ordinary file containing 64 hexadecimal characters with an optional LF or CRLF.'
    }
    Assert-OrdinaryDirectoryChain $keyItem.DirectoryName
    $keyBytes = [IO.File]::ReadAllBytes($keyFile)
    try {
        $keyValid = $keyBytes.Length -ge 64 -and $keyBytes.Length -le 66
        if ($keyValid) {
            for ($index = 0; $index -lt 64; ++$index) {
                $value = $keyBytes[$index]
                if (-not (($value -ge 48 -and $value -le 57) -or
                          ($value -ge 65 -and $value -le 70) -or
                          ($value -ge 97 -and $value -le 102))) {
                    $keyValid = $false
                    break
                }
            }
        }
        if ($keyBytes.Length -eq 65) { $keyValid = $keyValid -and $keyBytes[64] -eq 10 }
        if ($keyBytes.Length -eq 66) { $keyValid = $keyValid -and $keyBytes[64] -eq 13 -and $keyBytes[65] -eq 10 }
        if (-not $keyValid) {
            throw 'The key must contain exactly 64 hexadecimal characters with an optional LF or CRLF.'
        }
    } finally {
        [Array]::Clear($keyBytes, 0, $keyBytes.Length)
        $keyBytes = $null
        $value = 0
    }
}
$containerName = "post-profit-exchange-$environmentId"
$containerRoot = '/workspace/exchange-source'
$maskPaths = @("$containerRoot/projects/post-profit-exchange/environments")
$maskOptions = 'ro,nosuid,nodev,noexec,size=1m,mode=0555'
$portKey = "$Port/tcp"
$image = 'sha256:a12443bbcfb1d1acba2fbe63679b57422635dd6e88161cfaaf4554256fc1f9b4'
$baseImage = 'debian@sha256:6ebd97fa83deb272194a2cf015b3d26a4d538e9ad3a7a79d544c8af5b0a01443'
$sourceId = 'fb76bcc496b41c383fd4363cb5c2af15a877076beb4254d2af957dab6eba4b24'
$command = @('/bin/bash', 'projects/post-profit-exchange/code/interfaces/run-service.sh', '/operation/exchange.cfg')
function Invoke-Docker {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker failed (exit $LASTEXITCODE); existing state is preserved." }
}
function Assert-LocalPortAvailable {
    param([string]$OwnContainerId = '')
    $runningIds = @(Invoke-Docker ps -q --no-trunc)
    $ownRunning = $false
    if ($runningIds.Count -gt 0) {
        $running = @(Invoke-Docker inspect --type container @runningIds | ConvertFrom-Json)
        foreach ($other in $running) {
            if ($other.Id -ceq $OwnContainerId) { $ownRunning = $true; continue }
            foreach ($published in @($other.NetworkSettings.Ports.PSObject.Properties)) {
                foreach ($binding in @($published.Value)) {
                    if ($null -ne $binding -and $binding.HostPort -ceq [string]$Port) {
                        throw "Port $Port is published by '$($other.Name.TrimStart('/'))' ($($other.Id)); that container is preserved. Change runtime.server.port in exchange.cfg."
                    }
                }
            }
        }
    }
    # A matching running container is expected to own its validated host binding.
    if (-not $ownRunning) {
        try {
            $listeners = [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners()
        } catch {
            throw 'Could not inspect Windows listening ports; no container is created or started.'
        }
        if (@($listeners | Where-Object { $_.Port -eq $Port }).Count -gt 0) {
            throw "Windows already has a TCP listener on port $Port; existing services are preserved. Change runtime.server.port in exchange.cfg."
        }
    }
}
function Show-Configuration {
    Write-Host "Container: $containerName"
    Write-Host "Local recovered image: $image"
    Write-Host "Original base: $baseImage (Debian 12, Linux amd64)"
    Write-Host ("Command: " + ($command -join ' '))
    Write-Host "User: root; working directory: $containerRoot"
    Write-Host "Read-only source bind: $projectRoot -> $containerRoot"
    Write-Host "Read/write operation bind: $environmentHost -> /operation"
    foreach ($maskPath in $maskPaths) { Write-Host "Empty read-only tmpfs mask: $maskPath ($maskOptions)" }
    Write-Host "Read-only key file bind: $keyFile -> /run/secrets/exchange.key"
    Write-Host "Environment id: $environmentId; published port: 127.0.0.1:$Port -> $portKey"
    Write-Host 'Named volumes: none; restart: no; network: default bridge; shared memory: 64 MiB'
    Write-Host 'GPU and other devices: none; privileged: false; interactive/TTY: false'
    Write-Host 'Explicit provision verifies/installs dependencies only; normal up never runs setup or APT.'
    Write-Host 'Builds/cache stay under /opt/post-profit-exchange; existing database is never initialized on launch.'
    Write-Host 'The general post-profit-economics-gpu container is not changed.'
}
function Get-RecoveredImage {
    $info = (Invoke-Docker image inspect $image | ConvertFrom-Json)[0]
    if ($info.Id -cne $image -or $info.Os -cne 'linux' -or $info.Architecture -cne 'amd64' -or
        $info.Config.Volumes -or $info.Config.Entrypoint -or $info.Config.ExposedPorts -or
        $info.Config.Labels.'org.post-profit-economics.recovery-source' -cne $sourceId -or
        $info.Config.Labels.'org.post-profit-economics.base-image' -cne $baseImage) {
        throw 'Local recovery image identity or provenance differs; no pull or replacement is automatic.'
    }
    return $info
}
function Get-ManagedContainer {
    $ids = @(Invoke-Docker ps -aq --no-trunc --filter "name=^/$containerName`$")
    if ($ids.Count -eq 0) { return $null }
    if ($ids.Count -ne 1) { throw 'Expected one exact environment container name; nothing changed.' }
    $info = (Invoke-Docker inspect --type container $ids[0] | ConvertFrom-Json)[0]
    $imageInfo = Get-RecoveredImage
    $mounts = @($info.Mounts)
    $source = @($mounts | Where-Object { $_.Type -ceq 'bind' -and $_.Destination -ceq $containerRoot })
    $operation = @($mounts | Where-Object { $_.Type -ceq 'bind' -and $_.Destination -ceq '/operation' })
    $keyMount = @($mounts | Where-Object { $_.Type -ceq 'bind' -and $_.Destination -ceq '/run/secrets/exchange.key' })
    $tmpfsMount = @($mounts | Where-Object { $_.Type -ceq 'tmpfs' -and $_.Destination -cin $maskPaths -and -not $_.RW })
    $tmpfs = @($info.HostConfig.Tmpfs.PSObject.Properties)
    $ports = @($info.HostConfig.PortBindings.PSObject.Properties)
    $binding = @($info.HostConfig.PortBindings.$portKey)
    $networks = @($info.NetworkSettings.Networks.PSObject.Properties)
    $masksValid = $tmpfs.Count -eq $maskPaths.Count
    foreach ($maskPath in $maskPaths) {
        $mask = @($tmpfs | Where-Object { $_.Name -ceq $maskPath -and $_.Value -ceq $maskOptions })
        $masksValid = $masksValid -and $mask.Count -eq 1
    }
    $valid = $info.Id -ceq $ids[0] -and $info.Name -ceq "/$containerName" -and
        $info.Image -ceq $image -and $info.Config.Image -ceq $image -and
        $info.Config.Labels.'org.post-profit-economics.managed-by' -ceq 'post-profit-economics' -and
        $info.Config.Labels.'org.post-profit-economics.project-root' -ieq $projectRoot -and
        $info.Config.Labels.'org.post-profit-economics.configuration' -ceq 'exchange-environment-v3' -and
        $info.Config.Labels.'org.post-profit-economics.environment' -ceq $environmentPath -and
        $info.Config.Labels.'org.post-profit-economics.environment-id' -ceq $environmentId -and
        $info.Config.Labels.'org.post-profit-economics.recovery-source' -ceq $sourceId -and
        $info.Config.User -ceq 'root' -and $info.Config.WorkingDir -ceq $containerRoot -and
        (@($info.Config.Cmd) -join "`n") -ceq ($command -join "`n") -and
        -not $info.Config.Entrypoint -and -not $info.Config.OpenStdin -and -not $info.Config.Tty -and
        (@($info.Config.Env) -join "`n") -ceq (@($imageInfo.Config.Env) -join "`n") -and
        -not $info.Config.Volumes -and $source.Count -eq 1 -and $operation.Count -eq 1 -and
        $source[0].Source -ieq $projectRoot -and -not $source[0].RW -and
        $operation[0].Source -ieq $environmentHost -and $operation[0].RW -and
        $keyMount.Count -eq 1 -and $keyMount[0].Source -ieq $keyFile -and -not $keyMount[0].RW -and
        $tmpfsMount.Count -in @(0, 1) -and $mounts.Count -eq (3 + $tmpfsMount.Count) -and $masksValid -and
        -not $info.HostConfig.ReadonlyRootfs -and
        $info.HostConfig.ShmSize -eq 67108864 -and $info.HostConfig.RestartPolicy.Name -ceq 'no' -and
        $info.HostConfig.RestartPolicy.MaximumRetryCount -eq 0 -and -not $info.HostConfig.AutoRemove -and
        -not $info.HostConfig.Privileged -and -not $info.HostConfig.CapAdd -and
        -not $info.HostConfig.CapDrop -and -not $info.HostConfig.SecurityOpt -and
        $info.HostConfig.NetworkMode -in @('default', 'bridge') -and
        $networks.Count -eq 1 -and $networks[0].Name -ceq 'bridge' -and -not $info.HostConfig.PublishAllPorts -and
        -not $info.HostConfig.DeviceRequests -and -not $info.HostConfig.Devices -and
        -not $info.HostConfig.DeviceCgroupRules -and $ports.Count -eq 1 -and
        $ports[0].Name -ceq $portKey -and $binding.Count -eq 1 -and
        $binding[0].HostIp -ceq '127.0.0.1' -and $binding[0].HostPort -ceq [string]$Port
    if (-not $valid) {
        throw "Container '$containerName' ($($info.Id)) has mismatched configuration; preserved without changes."
    }
    return $info
}
if ($Action -eq 'plan') {
    $null = Get-RecoveredImage
    Show-Configuration
    $existing = Get-ManagedContainer
    if ($null -eq $existing) { Write-Host 'Existing environment container: absent' }
    else { Write-Host "Existing matching container: $($existing.Id) $($existing.State.Status)" }
    Assert-LocalPortAvailable -OwnContainerId $existing.Id
    Write-Output 'Plan only: no container was created, started or changed.'
    return
}
$container = Get-ManagedContainer
if ($Action -in @('provision', 'up')) {
    Show-Configuration
    if ($null -eq $container) {
        if ($Action -ne 'provision') { throw 'Environment container is absent. Use explicit -Action provision once.' }
        $null = Get-RecoveredImage
        Assert-LocalPortAvailable
        $createArguments = @('create', '--pull', 'never', '--name', $containerName, '--user', 'root',
            '--restart', 'no', '--network', 'bridge', '--shm-size', '64m',
            '--label', 'org.post-profit-economics.managed-by=post-profit-economics',
            '--label', "org.post-profit-economics.project-root=$projectRoot",
            '--label', 'org.post-profit-economics.configuration=exchange-environment-v3',
            '--label', "org.post-profit-economics.environment=$environmentPath",
            '--label', "org.post-profit-economics.environment-id=$environmentId",
            '--label', "org.post-profit-economics.recovery-source=$sourceId",
            '--mount', "type=bind,source=$projectRoot,target=$containerRoot,readonly",
            '--mount', "type=bind,source=$environmentHost,target=/operation",
            '--mount', "type=bind,source=$keyFile,target=/run/secrets/exchange.key,readonly")
        foreach ($maskPath in $maskPaths) { $createArguments += @('--tmpfs', "${maskPath}:$maskOptions") }
        $createArguments += @('--workdir', $containerRoot, '--publish', "${bindAddress}:${Port}:${Port}/tcp", $image) + $command
        $createdId = Invoke-Docker @createArguments
        $container = Get-ManagedContainer
        if ($null -eq $container -or $container.Id -cne $createdId) {
            throw 'Created identity could not be verified; container and records are preserved.'
        }
    }
    $id = $container.Id
    Assert-LocalPortAvailable -OwnContainerId $id
    if (-not $container.State.Running) { Invoke-Docker start $id | Out-Host }
    if ($Action -eq 'provision') {
        Invoke-Docker exec $id /bin/bash projects/post-profit-exchange/code/setup.sh | Out-Host
    } else {
        # Fixed argv avoids Windows PowerShell 5's native quoting of shell programs.
        # Normal up only compares the dependency marker; it never invokes setup/APT.
        try {
            $markerLines = @(Invoke-Docker exec $id cat /opt/post-profit-exchange/environment/dependencies.ready)
            $setupLines = @(Invoke-Docker exec $id sha256sum projects/post-profit-exchange/code/setup.sh)
        } catch {
            throw 'Dependencies could not be verified. Use -Action provision; normal up never runs APT.'
        }
        if ($markerLines.Count -ne 1 -or $markerLines[0] -cnotmatch '^[0-9a-f]{64}$' -or
            $setupLines.Count -ne 1 -or
            $setupLines[0] -cnotmatch '^[0-9a-f]{64}  projects/post-profit-exchange/code/setup\.sh$' -or
            $markerLines[0] -cne $setupLines[0].Substring(0, 64)) {
            throw 'Dependencies are not provisioned for this setup version. Use -Action provision; normal up never runs APT.'
        }
    }
    $deadline = [DateTime]::UtcNow.AddMinutes(3)
    do {
        $container = Get-ManagedContainer
        if ($null -eq $container -or $container.Id -cne $id -or -not $container.State.Running) {
            throw 'Application exited before readiness; preserve it and inspect -Action logs.'
        }
        $ready = $false
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$Port/api/state" `
                -TimeoutSec 2 -MaximumRedirection 0
            $snapshot = $response.Content | ConvertFrom-Json
            $ready = $response.StatusCode -eq 200 -and $snapshot.environment.environment_id -ceq $environmentId
        } catch {
            # The application may still be compiling or opening its database.
        }
        if ($ready) {
            Write-Output "$containerName $id ready at http://127.0.0.1:$Port/"
            if ($OpenBrowser) { Start-Process "http://127.0.0.1:$Port/" }
            return
        }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Application did not confirm readiness in three minutes; state preserved. Inspect -Action logs.'
}
if ($null -eq $container) {
    if ($Action -eq 'status') { Write-Output "$containerName absent"; return }
    throw 'Environment container is absent; nothing changed.'
}
switch ($Action) {
    'status' { Write-Output "$containerName $($container.Id) $($container.State.Status)" }
    'stop' { if ($container.State.Running) { Invoke-Docker stop $container.Id } }
    'logs' { Invoke-Docker logs --tail 100 $container.Id }
}
