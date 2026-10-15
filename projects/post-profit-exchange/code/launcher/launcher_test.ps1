# SPDX-License-Identifier: MIT
# Host orchestration contract tests; no container is created, started or stopped.
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$launcher = Join-Path $repository 'projects/post-profit-exchange/code/interfaces/application-container.ps1'
$parent = Join-Path $repository 'projects/post-profit-exchange/environments'
$identity = 'launcher-test-' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$fixture = [IO.Path]::GetFullPath((Join-Path $parent $identity))
if ([IO.Path]::GetDirectoryName($fixture) -ine $parent -or (Test-Path -LiteralPath $fixture)) {
    throw 'Fixture path validation failed.'
}
$null = New-Item -ItemType Directory -Path $fixture
$config = Join-Path $fixture 'exchange.cfg'
$missingKey = Join-Path ([IO.Path]::GetTempPath()) ($identity + '.missing-key')
if (Test-Path -LiteralPath $missingKey) { throw 'Expected absent test key path.' }
$settings = @{
    schema_version = 'exchange.environment.v2'
    environment_id = $identity
    runtime = @{
        workspace = '.'
        server = @{ bind = '127.0.0.1'; port = 8766 }
        encryption = @{ required = $true; key_file = $missingKey }
    }
}
$baseline = $settings | ConvertTo-Json -Depth 10
$cases = @(
    @{ name = 'missing key'; mutate = {}; expected = 'Cannot find path' },
    @{ name = 'nonhex key'; mutate = {}; key = ('z' * 64); expected = 'key must contain exactly 64 hexadecimal' },
    @{ name = 'short key'; mutate = {}; key = 'bad'; expected = 'key must be an ordinary file containing 64 hexadecimal' },
    @{ name = 'CR-only key ending'; mutate = {}; key = ('a' * 64) + [char]13; expected = 'key must contain exactly 64 hexadecimal' },
    @{ name = 'non-loopback bind'; mutate = { param($v) $v.runtime.server.bind = '0.0.0.0' }; expected = 'runtime.server requires' },
    @{ name = 'fractional port'; mutate = { param($v) $v.runtime.server.port = 8766.5 }; expected = 'runtime.server requires' },
    @{ name = 'plaintext disabled'; mutate = { param($v) $v.runtime.encryption.required = $false }; expected = 'requires required=true' },
    @{ name = 'relative key'; mutate = { param($v) $v.runtime.encryption.key_file = 'key.txt' }; expected = 'key_file must be absolute' },
    @{ name = 'other expansion'; mutate = { param($v) $v.runtime.encryption.key_file = '%TEMP%/key.txt' }; expected = 'key_file must be absolute' },
    @{ name = 'repository key'; mutate = { param($v) $v.runtime.encryption.key_file = Join-Path $repository 'test.key' }; expected = 'Keep the key file outside' },
    @{ name = 'workspace escape'; mutate = { param($v) $v.runtime.workspace = '..' }; expected = 'configuration must belong' },
    @{ name = 'identity mismatch'; mutate = { param($v) $v.environment_id = 'another-vendor' }; expected = 'configuration must belong' }
)
$passed = 0
try {
    foreach ($case in $cases) {
        $value = $baseline | ConvertFrom-Json
        & $case.mutate $value
        if ($case.ContainsKey('key')) {
            [IO.File]::WriteAllBytes($missingKey, [Text.Encoding]::ASCII.GetBytes($case.key))
        }
        [IO.File]::WriteAllText($config, ($value | ConvertTo-Json -Depth 10))
        $caught = ''
        try { & $launcher -Action plan -Config $config } catch { $caught = $_.Exception.Message }
        if (-not $caught.Contains($case.expected)) {
            throw "Failed '$($case.name)': expected '$($case.expected)', observed '$caught'."
        }
        ++$passed
        if (Test-Path -LiteralPath $missingKey) { Remove-Item -LiteralPath $missingKey }
    }
    Remove-Item -LiteralPath $config
    $caught = ''
    try { & $launcher -Action plan -Config $config } catch { $caught = $_.Exception.Message }
    if (-not $caught.Contains('Cannot find path')) { throw 'Missing configuration was not rejected.' }
    ++$passed
    Write-Output "Launcher validation: $passed negative cases passed; no Docker operation reached."
} finally {
    if (Test-Path -LiteralPath $missingKey) { Remove-Item -LiteralPath $missingKey }
    if (Test-Path -LiteralPath $config) { Remove-Item -LiteralPath $config }
    # Intentionally nonrecursive: unexpected fixture content is preserved.
    Remove-Item -LiteralPath $fixture
}
