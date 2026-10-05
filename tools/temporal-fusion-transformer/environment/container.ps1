param(
    [ValidateSet('plan', 'up', 'status', 'stop', 'shell', 'exec')]
    [string]$Action = 'status',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Command
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')).TrimEnd('\', '/')
$approvedRoot = 'C:\Work\post-profit-economics'
$rootItem = Get-Item -LiteralPath $projectRoot -Force
if ($projectRoot -ine $approvedRoot -or -not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw "Use the real renamed checkout '$approvedRoot'; nothing changed."
}
$containerName = 'post-profit-economics-gpu'
$containerRoot = '/workspace/praesidium-humanitatis'
$baseImage = 'debian@sha256:6ebd97fa83deb272194a2cf015b3d26a4d538e9ad3a7a79d544c8af5b0a01443'
$manifestPath = Join-Path $projectRoot '.build/environment/gpu-recovery.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "The local recovered GPU image manifest is missing at '$manifestPath'. Restore that environment; no empty replacement or dependency installation is automatic."
}
$recovery = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$image = [string]$recovery.image_id
$sourceId = [string]$recovery.source_container_id
if ($image -notmatch '^sha256:[0-9a-f]{64}$' -or $sourceId -cne 'fb76bcc496b41c383fd4363cb5c2af15a877076beb4254d2af957dab6eba4b24' -or
    $recovery.base_image -cne $baseImage -or $recovery.container_name -cne $containerName -or $recovery.container_root -cne $containerRoot -or
    $recovery.canonical_root -ine $projectRoot) {
    throw 'The GPU recovery manifest does not match the approved configuration; nothing changed.'
}
function Show-Configuration {
    Write-Host "Container: $containerName"
    Write-Host "Recovered local image: $image"
    Write-Host "Original Debian 12 base: $baseImage"
    Write-Host 'Command: /bin/bash, interactive and TTY; user: root'
    Write-Host "Bind mount: $projectRoot -> $containerRoot (read/write); working directory: $containerRoot"
    Write-Host 'GPU access: all NVIDIA GPUs; other devices: none; shared memory: 1 GiB'
    Write-Host 'Named volumes: none; published ports: none; restart policy: no; network: default bridge; privileged: false'
}
function Invoke-Docker {
    & docker @args
    if ($LASTEXITCODE -ne 0) { throw "Docker failed (exit $LASTEXITCODE); existing state is preserved." }
}
function Get-RecoveredImage {
    $info = (Invoke-Docker image inspect $image | ConvertFrom-Json)[0]
    if ($info.Id -cne $image -or $info.Os -cne 'linux' -or $info.Architecture -cne 'amd64' -or $info.Config.Volumes -or
        $info.Config.Labels.'org.post-profit-economics.recovery-source' -cne $sourceId -or
        $info.Config.Labels.'org.post-profit-economics.base-image' -cne $baseImage) {
        throw 'Recovered image identity or provenance differs; existing state is preserved.'
    }
    return $info
}
function Get-ManagedContainer {
    $ids = @(Invoke-Docker ps -aq --no-trunc --filter "name=^/$containerName`$")
    if ($ids.Count -eq 0) { return $null }
    if ($ids.Count -ne 1) { throw 'Expected one exact GPU container name; nothing changed.' }
    $info = (Invoke-Docker inspect --type container $ids[0] | ConvertFrom-Json)[0]
    $null = Get-RecoveredImage
    $mounts = @($info.Mounts)
    $gpu = @($info.HostConfig.DeviceRequests | Where-Object { $null -ne $_ })
    $ports = @($info.HostConfig.PortBindings.PSObject.Properties)
    $valid = $info.Id -ceq $ids[0] -and $info.Name -ceq "/$containerName" -and
        $info.Config.Image -ceq $image -and $info.Image -ceq $image -and
        $info.Config.Labels.'org.post-profit-economics.managed-by' -ceq 'post-profit-economics' -and
        $info.Config.Labels.'org.post-profit-economics.project-root' -ieq $projectRoot -and
        $info.Config.Labels.'org.post-profit-economics.configuration' -ceq '1' -and
        $info.Config.Labels.'org.post-profit-economics.libtorch' -ceq '2.6.0+cu124-cxx11' -and
        $info.Config.Labels.'org.post-profit-economics.recovery-source' -ceq $sourceId -and
        $info.Config.User -in @('', 'root', '0') -and $info.Config.WorkingDir -ceq $containerRoot -and
        @($info.Config.Cmd).Count -eq 1 -and $info.Config.Cmd[0] -ceq '/bin/bash' -and
        -not $info.Config.Entrypoint -and $info.Config.OpenStdin -and $info.Config.Tty -and
        -not $info.Config.Volumes -and $mounts.Count -eq 1 -and $mounts[0].Type -ceq 'bind' -and
        $mounts[0].Source -ieq $projectRoot -and $mounts[0].Destination -ceq $containerRoot -and $mounts[0].RW -and
        -not $info.HostConfig.ReadonlyRootfs -and -not $info.HostConfig.Tmpfs.PSObject.Properties -and
        $info.HostConfig.ShmSize -eq 1073741824 -and $info.HostConfig.RestartPolicy.Name -ceq 'no' -and
        $info.HostConfig.RestartPolicy.MaximumRetryCount -eq 0 -and -not $info.HostConfig.AutoRemove -and
        -not $info.HostConfig.Privileged -and -not $info.HostConfig.CapAdd -and -not $info.HostConfig.SecurityOpt -and
        $info.HostConfig.NetworkMode -in @('default', 'bridge') -and -not $info.HostConfig.PublishAllPorts -and
        $gpu.Count -eq 1 -and $gpu[0].Count -eq -1 -and $gpu[0].Driver -in @('', 'nvidia') -and
        -not $gpu[0].DeviceIDs -and @($gpu[0].Capabilities).Count -eq 1 -and
        (@($gpu[0].Capabilities[0]) -join ',') -ceq 'gpu' -and -not $info.HostConfig.Devices -and
        -not $info.HostConfig.DeviceCgroupRules -and $ports.Count -eq 0
    if (-not $valid) { throw "Container '$containerName' ($($info.Id)) has mismatched configuration; preserved without changes." }
    return $info
}
if ($Action -eq 'plan') { Show-Configuration; return }
$container = Get-ManagedContainer
if ($Action -eq 'up') {
    Show-Configuration
    if ($null -eq $container) {
        $null = Get-RecoveredImage
        $createArguments = @('create', '--name', $containerName, '--interactive', '--tty', '--user', 'root',
            '--gpus', 'all', '--shm-size', '1g', '--restart', 'no',
            '--label', 'org.post-profit-economics.managed-by=post-profit-economics',
            '--label', "org.post-profit-economics.project-root=$projectRoot",
            '--label', 'org.post-profit-economics.configuration=1',
            '--label', 'org.post-profit-economics.libtorch=2.6.0+cu124-cxx11',
            '--label', "org.post-profit-economics.recovery-source=$sourceId",
            '--mount', "type=bind,source=$projectRoot,target=$containerRoot", '--workdir', $containerRoot, $image, '/bin/bash')
        $createdId = Invoke-Docker @createArguments
        $container = Get-ManagedContainer
        if ($null -eq $container -or $container.Id -cne $createdId) { throw 'Created identity could not be verified; state preserved.' }
    }
    $id = $container.Id
    if (-not $container.State.Running) { Invoke-Docker start $id | Out-Host }
    $normalization = Invoke-Docker exec --workdir / $id /bin/bash /workspace/praesidium-humanitatis/tools/temporal-fusion-transformer/environment/normalize-bind.sh
    if ($normalization -ceq 'restart-required') { Invoke-Docker restart $id | Out-Host }
    elseif ($normalization -cne 'ready') { throw 'Unexpected Linux bind normalization result; state preserved.' }
    $container = Get-ManagedContainer
    if ($null -eq $container -or $container.Id -cne $id -or -not $container.State.Running) { throw 'GPU start verification failed; state preserved.' }
    Invoke-Docker exec --workdir $containerRoot $id /bin/bash tools/temporal-fusion-transformer/environment/configure-recovered.sh
    Write-Output "$containerName $id $($container.State.Status)"
    return
}
if ($null -eq $container) {
    if ($Action -eq 'status') { Write-Output "$containerName absent"; return }
    throw 'The GPU container is absent. Use up to reuse the saved local recovery image; no dependencies are installed automatically.'
}
switch ($Action) {
    'status' { Write-Output "$containerName $($container.Id) $($container.State.Status)" }
    'stop' { if ($container.State.Running) { Invoke-Docker stop $container.Id } }
    'shell' {
        if (-not $container.State.Running) { throw 'Container is stopped. Run this launcher with up.' }
        Invoke-Docker exec -it --workdir $containerRoot $container.Id /bin/bash
    }
    'exec' {
        if (-not $container.State.Running) { throw 'Container is stopped. Run this launcher with up.' }
        if (-not $Command) { throw 'Supply -Action exec -Command @(...).' }
        Invoke-Docker exec --workdir $containerRoot $container.Id @Command
    }
}
