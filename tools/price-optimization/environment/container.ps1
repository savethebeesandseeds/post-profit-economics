# SPDX-License-Identifier: MIT AND LicenseRef-Praesidium-Humanitatis-Worker-Protection-1.0
# Copyright (c) 2026 Waajacu
# Adapted from the MIT TFT launcher; see NOTICE.md and LICENSE-MIT-TFT-LAUNCHER.
# Lifecycle is shared with TFT. This entry never creates a CPU container.
param(
    [ValidateSet('plan', 'up', 'status', 'stop', 'shell', 'exec')]
    [string]$Action = 'status',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Command
)
$ErrorActionPreference = 'Stop'
$gpuLauncher = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../temporal-fusion-transformer/environment/container.ps1'))
& $gpuLauncher -Action $Action -Command $Command
