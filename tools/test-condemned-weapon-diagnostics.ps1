<#
.SYNOPSIS
    Self-contained schema regression for the live weapon diagnostic watcher.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Assert-Equal($Expected, $Actual, [string]$Label) {
    if ($Expected -ne $Actual) {
        throw "$Label expected '$Expected', got '$Actual'."
    }
}

function Assert-Near(
    [double]$Expected,
    [double]$Actual,
    [double]$Tolerance,
    [string]$Label) {
    if ([Math]::Abs($Expected - $Actual) -gt $Tolerance) {
        throw "$Label expected '$Expected', got '$Actual'."
    }
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'condemned-vr-weapon-diagnostics-test-' +
    [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null

try {
    $loaderLog = Join-Path $testRoot 'loader.jsonl'
    $events = @(
        [pscustomobject]@{
            event = 'm5_physical_melee_auto_seed_armed'
            detail = (
                'scope=verified_mapped_one_handed_equip ' +
                'max_attempts=3')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_profile_selected'
            detail = 'profile=pipe weapon_index=32'
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_auto_seed_candidate'
            detail = (
                'weapon_index=32 weapon=0x10000000 ' +
                'model=0x20000000 candidate_valid=1 ' +
                'phase=stabilizing attempts=0 collision_body_ready=0')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_auto_seed_started'
            detail = (
                'count=1 weapon_index=32 attempt=1/3 ' +
                'output=retail_fire_command_17 haptic=blocked ' +
                'native_impact_dispatch=blocked')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_auto_seed_confirmed'
            detail = (
                'count=1 weapon_index=32 collision_object=0x11111111 ' +
                'attempt=1 source=automatic_equip_pulse ' +
                'player_attack_classified=1 native_read_mask=0x7 ' +
                'phase=settling ready_immediately=0')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_auto_seed_impact_blocked'
            detail = (
                'count=1 impact_controller=0x30000000 ' +
                'native_forwarded=0 contact_latch_mutated=0')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_auto_seed_ready'
            detail = (
                'count=1 weapon_index=32 attempt=1 phase=ready ' +
                'collision_body_live=1 native_read_mask=0x7')
        },
        [pscustomobject]@{
            event = 'm5_weapon_test_collider_state'
            detail = 'seeded=1 collision_object=0x11111111'
        },
        [pscustomobject]@{
            event = 'm5_weapon_test_collider_state'
            detail = 'seeded=1 collision_object=0x22222222'
        },
        [pscustomobject]@{
            event = 'm5_weapon_test_collider_state'
            detail = 'seeded=1 collision_object=0x11111111'
        },
        [pscustomobject]@{
            event = 'm5_weapon_test_collider_state'
            detail = 'seeded=1 collision_object=0x22222222'
        },
        [pscustomobject]@{
            event = 'm5_weapon_test_collider_state'
            detail = 'seeded=1 collision_object=0x11111111'
        },
        [pscustomobject]@{
            event = 'm5_aim_path_command_edge'
            detail = (
                'command=17 edge=down controller_applied=1 ' +
                'retail_value=0 output_value=1 runtime_tick_ms=10')
        },
        [pscustomobject]@{
            event = 'm5_aim_path_command_edge'
            detail = (
                'command=17 edge=up controller_applied=0 ' +
                'retail_value=0 output_value=0 runtime_tick_ms=20')
        },
        [pscustomobject]@{
            event = 'm5_combat_player_vitals'
            detail = (
                'runtime_tick_ms=100 current=100 maximum=100 ' +
                'fraction=1.0000 delta=0 initial=1 cause=unattributed')
        },
        [pscustomobject]@{
            event = 'm5_aim_path_command_edge'
            detail = (
                'command=28 edge=down controller_applied=1 ' +
                'retail_value=0 output_value=1 runtime_tick_ms=100')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_block_pose_state'
            detail = (
                'weapon_index=32 active=1 entered=1 exited=0 ' +
                'tracking_fresh=1 position_error_m=0.031 ' +
                'angle_error_deg=4.5 position_tolerance_m=0.18 ' +
                'angle_tolerance_deg=25 reason=matched command=28 ' +
                'source=automatic_guard_pose input_seed_required=0 ' +
                'manual_trigger_fallback=1')
        },
        [pscustomobject]@{
            event = 'm5_combat_player_vitals'
            detail = (
                'runtime_tick_ms=200 current=95 maximum=100 ' +
                'fraction=0.9500 delta=-5 initial=0 cause=unattributed')
        },
        [pscustomobject]@{
            event = 'm5_aim_path_command_edge'
            detail = (
                'command=28 edge=up controller_applied=1 ' +
                'retail_value=0 output_value=0 runtime_tick_ms=350')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_block_pose_state'
            detail = (
                'weapon_index=32 active=0 entered=0 exited=1 ' +
                'tracking_fresh=1 position_error_m=0.280 ' +
                'angle_error_deg=31 position_tolerance_m=0.18 ' +
                'angle_tolerance_deg=25 reason=position_and_angle_outside ' +
                'command=28 source=automatic_guard_pose ' +
                'input_seed_required=0 manual_trigger_fallback=1')
        },
        [pscustomobject]@{
            event = 'm5_combat_player_vitals'
            detail = (
                'runtime_tick_ms=300 current=85 maximum=100 ' +
                'fraction=0.8500 delta=-10 initial=0 cause=unattributed')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_swing_attack_triggered'
            detail = (
                'trigger=1 speed_mps=8.0 threshold_mps=3.0 ' +
                'pulse_ms=100 cooldown_ms=450')
        },
        [pscustomobject]@{
            event = 'm5_weapon_test_contact'
            detail = (
                'target=0x22222222 target_node=0x1 ' +
                'target_kind=actor_candidate accepted=1 reason=accepted ' +
                'native_forwarded=1 retail_ref_vector_clear_ok=1 ' +
                'retail_ref_vector_state=cleared retail_refs_before=1 ' +
                'retail_refs_cleared=1 contact_damage_active=1 ' +
                'damage_dispatch_count=1 runtime_tick_ms=1000 ' +
                'contact_position_valid=1 contact_distance_valid=1 ' +
                'weapon_tip_to_target_contact_m=0.01 ' +
                'weapon_axis_to_target_contact_m=0.01 ' +
                'weapon_capsule_to_target_gap_m=-0.01 ' +
                'weapon_capsule_radius_m=0.025 contact_axis_t=0.5 ' +
                'head_pose_valid=1 head_to_target_contact_m=0.80 ' +
                'head_horizontal_to_target_contact_m=0.65 ' +
                'grip_pose_valid=1 grip_to_target_contact_m=0.30 ' +
                'attack_telegraph_enabled=1 ' +
                'attack_telegraph_triggered_this_swing=1 ' +
                'attack_telegraph_pulse_active=1 ' +
                'enemy_health_observed=0 speed_mps=8.0 energy_j=20.0')
        },
        [pscustomobject]@{
            event = 'm5_physical_melee_contact_rearmed'
            detail = (
                'reason=swing_completed tip_displacement_m=0.2 ' +
                'max_tip_displacement_m=0.3 required_tip_travel_m=0.12 ' +
                'speed_mps=0.5 release_speed_mps=2.0 release_samples=3/3')
        },
        [pscustomobject]@{
            event = 'm5_weapon_test_contact'
            detail = (
                'target=0x22222222 target_node=0x2 ' +
                'target_kind=actor_candidate accepted=1 reason=accepted ' +
                'native_forwarded=1 retail_ref_vector_clear_ok=1 ' +
                'retail_ref_vector_state=cleared retail_refs_before=1 ' +
                'retail_refs_cleared=1 contact_damage_active=1 ' +
                'damage_dispatch_count=2 runtime_tick_ms=2400 ' +
                'contact_position_valid=1 contact_distance_valid=1 ' +
                'weapon_tip_to_target_contact_m=0.02 ' +
                'weapon_axis_to_target_contact_m=0.02 ' +
                'weapon_capsule_to_target_gap_m=-0.005 ' +
                'weapon_capsule_radius_m=0.025 contact_axis_t=0.6 ' +
                'head_pose_valid=1 head_to_target_contact_m=0.70 ' +
                'head_horizontal_to_target_contact_m=0.55 ' +
                'grip_pose_valid=1 grip_to_target_contact_m=0.25 ' +
                'attack_telegraph_enabled=1 ' +
                'attack_telegraph_triggered_this_swing=0 ' +
                'attack_telegraph_pulse_active=0 ' +
                'enemy_health_observed=0 speed_mps=7.5 energy_j=18.0')
        }
    )
    [IO.File]::WriteAllLines(
        $loaderLog,
        @($events | ForEach-Object {
            $_ | ConvertTo-Json -Compress
        }),
        (New-Object Text.UTF8Encoding($false)))

    $liveReport = [pscustomobject]@{
        GameProcessId = 0
        LoaderLog = $loaderLog
        WeaponAlignmentCommand = ''
    }
    [IO.File]::WriteAllText(
        (Join-Path $testRoot 'm2-mono-live.json'),
        ($liveReport | ConvertTo-Json),
        (New-Object Text.UTF8Encoding($false)))

    & "$PSScriptRoot\watch-condemned-weapon-diagnostics.ps1" `
        -Run $testRoot -Once | Out-Null
    $snapshot = Get-Content -Raw -LiteralPath (
        Join-Path $testRoot 'weapon-diagnostics-live.json') |
        ConvertFrom-Json
    $target = @($snapshot.Targets)[0]
    $eventTimeline = @(Get-Content -LiteralPath (
        Join-Path $testRoot 'weapon-diagnostics-events.jsonl') |
        ConvertFrom-Json)
    $colliderTimeline = @($eventTimeline | Where-Object Event -eq 'collider_state')

    Assert-Equal 4 $snapshot.SchemaVersion 'schema version'
    Assert-Equal 5 $snapshot.Collider.StateEventsObserved (
        'collider state events observed')
    Assert-Equal 2 $snapshot.Collider.StateEventsRecorded (
        'collider state events recorded')
    Assert-Equal 3 $snapshot.Collider.StateEventsSuppressed (
        'collider state events suppressed')
    Assert-Equal $true ([bool]$snapshot.Collider.AutomaticSeed.Armed) (
        'automatic seed armed')
    Assert-Equal 'ready' $snapshot.Collider.AutomaticSeed.Phase (
        'automatic seed phase')
    Assert-Equal 1 $snapshot.Collider.AutomaticSeed.Attempts (
        'automatic seed attempts')
    Assert-Equal 1 $snapshot.Collider.AutomaticSeed.StartsObserved (
        'automatic seed starts')
    Assert-Equal 1 $snapshot.Collider.AutomaticSeed.ConfirmationsObserved (
        'automatic seed confirmations')
    Assert-Equal 1 $snapshot.Collider.AutomaticSeed.ReadyEventsObserved (
        'automatic seed ready events')
    Assert-Equal 1 $snapshot.Collider.AutomaticSeed.SeedImpactsBlocked (
        'automatic seed impacts blocked')
    Assert-Equal 'automatic_equip_pulse' (
        $snapshot.Collider.AutomaticSeed.LastConfirmationSource) (
        'automatic seed confirmation source')
    Assert-Equal $false ([bool](
        $snapshot.Collider.AutomaticSeed.ManualAttackFallback)) (
        'automatic seed manual fallback')
    Assert-Equal 2 $colliderTimeline.Count (
        'collider timeline compacted event count')
    Assert-Equal 1 $snapshot.Combat.RetailAttackCommandDownEdges (
        'Retail attack command down edges')
    Assert-Equal 1 $snapshot.Combat.RetailAttackCommandUpEdges (
        'Retail attack command up edges')
    Assert-Equal 1 $snapshot.Combat.AutomaticSwingAttackTriggers (
        'automatic swing attack triggers')
    Assert-Equal 2 @($eventTimeline | Where-Object Event -eq (
        'retail_attack_command_edge')).Count (
        'Retail attack command timeline edges')
    Assert-Equal 2 $snapshot.Combat.AcceptedActorHits 'actor hit count'
    Assert-Equal 1 $snapshot.Combat.AttackTelegraphTriggers (
        'attack telegraph triggers')
    Assert-Equal 1 $snapshot.Combat.AcceptedWithAttackTelegraph (
        'accepted with telegraph')
    Assert-Equal 1 $snapshot.Combat.AcceptedWithoutAttackTelegraph (
        'accepted without telegraph')
    Assert-Near 0.55 (
        [double]$snapshot.Combat.MinimumActorHeadHorizontalDistanceMeters) `
        0.0001 'minimum actor stand-off'
    Assert-Equal 1 $snapshot.Combat.BlockCommandDownEdges 'block down edges'
    Assert-Equal 1 $snapshot.Combat.BlockCommandUpEdges 'block up edges'
    Assert-Equal 250 $snapshot.Combat.LastBlockHoldDurationMilliseconds (
        'last block hold duration')
    Assert-Equal 250 $snapshot.Combat.MaximumBlockHoldDurationMilliseconds (
        'maximum block hold duration')
    Assert-Equal 250 (
        $snapshot.Combat.LastBlockCommandActivationDurationMilliseconds) (
        'last block command activation duration')
    Assert-Equal $false ([bool]$snapshot.Combat.NativeBlockStateObserved) (
        'native block state observation boundary')
    Assert-Equal $true ([bool]$snapshot.Combat.AutomaticBlockPose.Configured) (
        'automatic block pose configured')
    Assert-Equal $false ([bool]$snapshot.Combat.AutomaticBlockPose.Active) (
        'automatic block pose final state')
    Assert-Equal 1 $snapshot.Combat.AutomaticBlockPose.Activations (
        'automatic block pose activations')
    Assert-Equal 1 $snapshot.Combat.AutomaticBlockPose.Exits (
        'automatic block pose exits')
    Assert-Equal $false (
        [bool]$snapshot.Combat.AutomaticBlockPose.InputSeedRequired) (
        'automatic block pose seed requirement')
    Assert-Near 0.28 (
        [double]$snapshot.Combat.AutomaticBlockPose.PositionErrorMeters) `
        0.0001 'automatic block pose position error'
    Assert-Equal 2 @($eventTimeline | Where-Object Event -eq (
        'automatic_block_pose_state')).Count (
        'automatic block pose timeline edges')
    Assert-Equal 1 $snapshot.Combat.HealthDecreasesWhileBlocked (
        'health decreases while blocked')
    Assert-Equal 1 $snapshot.Combat.HealthDecreasesWhileNotBlocked (
        'health decreases while unblocked')
    Assert-Equal 85 $snapshot.Combat.PlayerVitals.Current (
        'latest player health')
    Assert-Equal 2 $target.AcceptedContacts 'per-target accepted contacts'
    Assert-Equal 1400 $target.LastAcceptedIntervalMilliseconds (
        'per-target accepted interval')
    Assert-Equal $false ([bool]$snapshot.Combat.EnemyHealthObserved) (
        'enemy health observation boundary')

    Write-Host (
        'Condemned weapon diagnostic watcher schema-v4 tests passed.') `
        -ForegroundColor Green
} finally {
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $resolvedTempRoot = [IO.Path]::GetFullPath(
        [IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedTestRoot.StartsWith(
            $resolvedTempRoot,
            [StringComparison]::OrdinalIgnoreCase) -or
        -not (Split-Path -Leaf $resolvedTestRoot).StartsWith(
            'condemned-vr-weapon-diagnostics-test-')) {
        throw "Refusing to remove unexpected test directory: $resolvedTestRoot"
    }
    if (Test-Path -LiteralPath $resolvedTestRoot -PathType Container) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
