function Resolve-CondemnedVrLaunchProfile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.IDictionary]$BoundParameters
    )

    # Parameters outside this list tune the selected launch or request a
    # rollback; they do not replace the default feature platform. Keeping this
    # list explicit prevents a new diagnostic from being enabled implicitly.
    $featureParameters = @(
        'WeaponTest',
        'RendererProbe',
        'RendererPassThrough',
        'StereoDiagnostic',
        'DoubleRenderDiagnostic',
        'CameraReadProbe',
        'EyeOffsetDiagnostic',
        'ReverseEyeOffsetDiagnostic',
        'ZeroEyeOffsetDiagnostic',
        'StereoTuning',
        'LocomotionProbe',
        'TurningProbe',
        'MenuProbe',
        'MenuControlsProbe',
        'RetailVrSettingsProbe',
        'InteractionProbe',
        'CoreActionsProbe',
        'ForensicMemoryProbe',
        'HapticsProbe',
        'HeadAimProbe',
        'AimPathProbe',
        'MeleeAimProbe',
        'PhysicalMeleeProbe',
        'PhysicalMeleeWallProxy',
        'PhysicalMeleeContactDamage',
        'PhysicalMeleeColliderDebug',
        'PhysicalMeleeVisualProxy',
        'WeaponGripCalibration',
        'WeaponCatalogProbe',
        'ArmIkDiscovery',
        'ArmIkRightHandProof',
        'ArmIkRightArm',
        'TwoHandedMelee',
        'PerformanceProbe',
        'RecenterProbe',
        'DesktopWindow'
    )
    $explicitFeatures = @(
        $featureParameters | Where-Object {
            $BoundParameters.Keys -contains $_
        })
    $minimal = $BoundParameters.Keys -contains 'Minimal' -and
        [bool]$BoundParameters['Minimal']
    if ($minimal -and $explicitFeatures.Count -ne 0) {
        throw ('-Minimal cannot be combined with feature-selection ' +
            'parameters: ' + ($explicitFeatures -join ', '))
    }
    if ($minimal) {
        return [pscustomobject][ordered]@{
            Name = 'Minimal'
            ApplyPipePreset = $false
            EnableRetailVrSettings = $false
            ExplicitFeatureSelection = $false
        }
    }
    if ($explicitFeatures.Count -eq 0) {
        return [pscustomobject][ordered]@{
            Name = 'Current'
            ApplyPipePreset = $true
            EnableRetailVrSettings = $true
            ExplicitFeatureSelection = $false
        }
    }
    $explicitPipeOnly = $explicitFeatures.Count -eq 1 -and
        $explicitFeatures[0] -eq 'WeaponTest' -and
        [string]$BoundParameters['WeaponTest'] -eq 'Pipe'
    return [pscustomobject][ordered]@{
        Name = $(if ($explicitPipeOnly) { 'Pipe' } else { 'Custom' })
        ApplyPipePreset = $false
        EnableRetailVrSettings = $false
        ExplicitFeatureSelection = $true
    }
}
