# SPDX-License-Identifier: MIT
# One application container per operating environment; shared development is separate.
param(
    [ValidateSet('plan', 'provision', 'up', 'status', 'stop', 'logs')]
    [string]$Action = 'plan',
    [string]$Environment = 'projects/post-profit-exchange/code/environments/vendor-operation',
    [ValidateRange(1024, 65535)][int]$Port = 8766
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..')).TrimEnd('\', '/')
$approvedRoot = 'C:\Work\post-profit-economics'
$rootItem = Get-Item -LiteralPath $projectRoot -Force
if ($projectRoot -ine $approvedRoot -or -not $rootItem.PSIsContainer -or
    ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw "Use the real checkout '$approvedRoot'; nothing changed."
}
$environmentPath = $Environment.Replace('\', '/')
$environmentParent = 'projects/post-profit-exchange/code/environments/'
if ($environmentPath -cnotmatch '^[A-Za-z0-9_-][A-Za-z0-9._-]*(/[A-Za-z0-9_-][A-Za-z0-9._-]*)*$' -or
    -not $environmentPath.StartsWith($environmentParent, [StringComparison]::Ordinal)) {
    throw 'Environment must be a directory under projects/post-profit-exchange/code/environments without traversal.'
}
$environmentHost = $projectRoot
foreach ($component in $environmentPath.Split('/')) {
    $environmentHost = [IO.Path]::GetFullPath((Join-Path $environmentHost $component))
    if (-not $environmentHost.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Environment escapes the repository; nothing changed.'
    }
    $item = Get-Item -LiteralPath $environmentHost -Force
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Environment directories must exist and cannot use reparse points.'
    }
}
$configPath = Join-Path $environmentHost 'exchange.cfg'
$configItem = Get-Item -LiteralPath $configPath -Force
if ($configItem.PSIsContainer -or ($configItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $configItem.Length -gt 1048576) {
    throw 'Environment configuration must be an ordinary file no larger than 1 MiB.'
}
$configText = Get-Content -LiteralPath $configPath -Raw
$config = $configText | ConvertFrom-Json
if ($config.schema_version -cne 'exchange.environment.v1' -or $config.environment_id -isnot [string] -or
    [regex]::Matches($configText, '"environment_id"\s*:').Count -ne 1 -or
    $config.environment_id.Length -gt 48 -or $config.environment_id -cnotmatch '^[a-z0-9]+(-[a-z0-9]+)*$') {
    throw 'Environment id must be unique lowercase letters/digits separated by hyphens, at most 48 characters.'
}
$environmentId = $config.environment_id
$containerName = "post-profit-exchange-$environmentId"
$containerRoot = '/workspace/exchange-source'
$maskPath = "$containerRoot/projects/post-profit-exchange/code/environments"
$maskOptions = 'ro,nosuid,nodev,noexec,size=1m,mode=0555'
$portKey = "$Port/tcp"
$image = 'sha256:a12443bbcfb1d1acba2fbe63679b57422635dd6e88161cfaaf4554256fc1f9b4'
$baseImage = 'debian@sha256:6ebd97fa83deb272194a2cf015b3d26a4d538e9ad3a7a79d544c8af5b0a01443'
$sourceId = 'fb76bcc496b41c383fd4363cb5c2af15a877076beb4254d2af957dab6eba4b24'
$command = @('/bin/bash', 'projects/post-profit-exchange/code/interfaces/run-service.sh', '/operation', $environmentId, [string]$Port)
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
                        throw "Port $Port is published by '$($other.Name.TrimStart('/'))' ($($other.Id)); that container is preserved. Choose another -Port."
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
            throw "Windows already has a TCP listener on port $Port; existing services are preserved. Choose another -Port."
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
    Write-Host "Empty read-only tmpfs mask: $maskPath ($maskOptions)"
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
    $tmpfsMount = @($mounts | Where-Object { $_.Type -ceq 'tmpfs' -and $_.Destination -ceq $maskPath -and -not $_.RW })
    $tmpfs = @($info.HostConfig.Tmpfs.PSObject.Properties)
    $ports = @($info.HostConfig.PortBindings.PSObject.Properties)
    $binding = @($info.HostConfig.PortBindings.$portKey)
    $networks = @($info.NetworkSettings.Networks.PSObject.Properties)
    $valid = $info.Id -ceq $ids[0] -and $info.Name -ceq "/$containerName" -and
        $info.Image -ceq $image -and $info.Config.Image -ceq $image -and
        $info.Config.Labels.'org.post-profit-economics.managed-by' -ceq 'post-profit-economics' -and
        $info.Config.Labels.'org.post-profit-economics.project-root' -ieq $projectRoot -and
        $info.Config.Labels.'org.post-profit-economics.configuration' -ceq 'exchange-environment-v2' -and
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
        $tmpfsMount.Count -in @(0, 1) -and $mounts.Count -eq (2 + $tmpfsMount.Count) -and
        $tmpfs.Count -eq 1 -and $tmpfs[0].Name -ceq $maskPath -and $tmpfs[0].Value -ceq $maskOptions -and
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
            '--label', 'org.post-profit-economics.configuration=exchange-environment-v2',
            '--label', "org.post-profit-economics.environment=$environmentPath",
            '--label', "org.post-profit-economics.environment-id=$environmentId",
            '--label', "org.post-profit-economics.recovery-source=$sourceId",
            '--mount', "type=bind,source=$projectRoot,target=$containerRoot,readonly",
            '--mount', "type=bind,source=$environmentHost,target=/operation",
            '--tmpfs', "${maskPath}:$maskOptions",
            '--workdir', $containerRoot, '--publish', "127.0.0.1:${Port}:${Port}/tcp", $image) + $command
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
        $check = @'
import hashlib, pathlib, sys
marker = pathlib.Path('/opt/post-profit-exchange/environment/dependencies.ready')
setup = pathlib.Path('projects/post-profit-exchange/code/setup.sh')
sys.exit(0 if marker.is_file() and marker.read_text().strip() == hashlib.sha256(setup.read_bytes()).hexdigest() else 1)
'@
        & docker exec $id python3 -c $check 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Dependencies are not provisioned for this setup version. Use -Action provision; normal up never runs APT.' }
    }
    $deadline = [DateTime]::UtcNow.AddMinutes(3)
    do {
        $container = Get-ManagedContainer
        if ($null -eq $container -or $container.Id -cne $id -or -not $container.State.Running) {
            throw 'Application exited before readiness; preserve it and inspect -Action logs.'
        }
        $health = @'
import sys, urllib.request
try:
    response = urllib.request.urlopen('http://127.0.0.1:' + sys.argv[1] + '/api/state', timeout=2)
    sys.exit(0 if response.status == 200 else 1)
except Exception:
    sys.exit(1)
'@
        & docker exec $id python3 -c $health ([string]$Port) 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Output "$containerName $id ready at http://127.0.0.1:$Port/"
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
