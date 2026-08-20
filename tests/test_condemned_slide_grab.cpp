#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

#include "condemned_slide_grab.h"
#include "condemned_tool_menu.h"

namespace {
bool Near(float a, float b, float e = 0.0002F) { return std::fabs(a-b)<=e; }
condemnedvr::SlideGrabFrameInput ValidFrame(const condemnedvr::SlideGrabRailSettings& s) {
    condemnedvr::SlideGrabFrameInput i{}; i.settings=s;
    i.controllerModelLocal=s.grabVolumeModelLocal.positionUnits;
    i.weaponIndex=condemnedvr::kColtSlideGrabWeaponIndex; i.sourceGeneration=7U;
    i.trackingFresh=i.focused=i.gamePlaying=i.exactWeaponIdentity=true;
    i.modelAvailable=i.nodeResolved=i.transformValid=true; return i;
}
}

int main() {
    using namespace condemnedvr;
    InteractionAuthoringPrimitive primitive =
        InteractionAuthoringPrimitive::MagazineInsertSocket;
    CycleInteractionAuthoringPrimitive(primitive, 1);
    assert(primitive == InteractionAuthoringPrimitive::SlideGrabRail);
    CycleInteractionAuthoringPrimitive(primitive, -1);
    assert(primitive ==
        InteractionAuthoringPrimitive::MagazineInsertSocket);
    SlideGrabRailSettings colt=ColtSlideGrabSeedSettings();
    assert(!colt.configured && std::strcmp(colt.nodeName,"SlideJnt")==0);
    assert(std::strcmp(kColtSlideParentName,"anim_cult45")==0);
    assert(Near(colt.closedPositionUnits.x,14.1689F));
    assert(Near(colt.closedPositionUnits.y,2.8062F));
    assert(Near(colt.closedPositionUnits.z,-8.7261F));
    assert(Near(colt.closedToRearAxis.x,-0.989379F));
    assert(Near(colt.closedToRearAxis.y,0.007748F));
    assert(Near(colt.closedToRearAxis.z,0.145151F));
    assert(Near(colt.maximumTravelUnits,3.8651F));
    assert(SlideGrabRailSettingsAreValid(colt));
    const auto rear=SlideRearEndpoint(colt);
    assert(Near(rear.x,10.3449F,0.0004F) && Near(rear.y,2.8362F,0.0004F));
    assert(Near(rear.z,-8.1651F,0.0004F));
    fearvr::TrackingVector normalized{};
    assert(NormalizeSlideAxis(colt.closedToRearAxis,normalized));
    assert(Near(PhysicalMeleeLength(normalized),1.0F));
    auto invalid=colt; invalid.closedToRearAxis={};
    assert(!SlideGrabRailSettingsAreValid(invalid));
    invalid=colt;
    invalid.closedToRearAxis=PhysicalMeleeScale(
        colt.closedToRearAxis,0.8F);
    assert(NormalizeSlideAxis(invalid.closedToRearAxis,normalized));
    assert(!SlideGrabRailSettingsAreValid(invalid));
    invalid=colt; invalid.closedToRearAxis.x=std::numeric_limits<float>::quiet_NaN();
    assert(!SlideGrabRailSettingsAreValid(invalid));

    colt.configured=true;
    colt.grabVolumeModelLocal.positionUnits={4,5,6};
    colt.grabVolumeModelLocal.rotation=PhysicalMeleeLocalRotationFromDegrees({0,90,0});
    colt.handPoseModelLocal.positionUnits={5,6,7};
    assert(SlideGrabVolumeContains(colt,{4,5,6}));
    assert(SlideGrabVolumeContains(colt,{6.9F,5,6}));
    assert(!SlideGrabVolumeContains(colt,{7.1F,5,6}));
    const fearvr::TrackingVector attach{1,2,3};
    const auto closed=ProjectSlideControllerDisplacement(colt,attach,attach);
    assert(closed.valid && Near(closed.clampedTravelUnits,0));
    const auto pulled=ProjectSlideControllerDisplacement(colt,attach,
        PhysicalMeleeAdd(attach,PhysicalMeleeScale(colt.closedToRearAxis,10)));
    assert(pulled.valid && Near(pulled.clampedTravelUnits,3.8651F));
    assert(pulled.rearReached && Near(pulled.slidePositionModelLocal.x,rear.x,0.0004F));
    assert(Near(pulled.handTargetModelLocal.positionUnits.x,
        colt.handPoseModelLocal.positionUnits.x+colt.closedToRearAxis.x*colt.maximumTravelUnits));
    const auto forward=ProjectSlideControllerDisplacement(colt,attach,
        PhysicalMeleeSubtract(attach,PhysicalMeleeScale(colt.closedToRearAxis,2)));
    assert(forward.valid && Near(forward.clampedTravelUnits,0));

    PhysicalMeleeRigidTransform modelWorld{{10,-4,20},PhysicalMeleeLocalRotationFromDegrees({10,20,-5})};
    const PhysicalMeleeRigidTransform cursorLocal{{2,3,4},PhysicalMeleeLocalRotationFromDegrees({5,-7,11})};
    PhysicalMeleeRigidTransform cursorWorld{},resolvedLocal{};
    assert(ComposePhysicalMeleeRigidTransforms(modelWorld,cursorLocal,cursorWorld));
    assert(ResolveMagazineSocketCursorModelLocal(modelWorld,cursorWorld,resolvedLocal));
    assert(PhysicalMeleeLength(PhysicalMeleeSubtract(resolvedLocal.positionUnits,cursorLocal.positionUnits))<0.0002F);
    fearvr::ArmIkTuning authoredGripTuning{};
    authoredGripTuning.leftHandRightMeters=0.01F;
    authoredGripTuning.leftHandUpMeters=0.02F;
    authoredGripTuning.leftHandForwardMeters=-0.03F;
    authoredGripTuning.leftHandYawDegrees=90.0F;
    const auto authoredHandTarget=ResolveToolMenuLeftHandIkTarget(
        cursorWorld,authoredGripTuning,100.0F);
    assert(PhysicalMeleeRigidTransformIsValid(authoredHandTarget));
    const fearvr::TrackingVector expectedAuthoredOffset=fearvr::Rotate(
        fearvr::Normalize(cursorWorld.rotation),{1.0F,2.0F,-3.0F});
    assert(PhysicalMeleeLength(PhysicalMeleeSubtract(
        authoredHandTarget.positionUnits,
        PhysicalMeleeAdd(cursorWorld.positionUnits,expectedAuthoredOffset)))<0.0002F);
    assert(!Near(authoredHandTarget.rotation.w,cursorWorld.rotation.w));

    SlideGrabEditorState editor{}; SetSlideGrabEditorValue(editor,colt,true);
    const PhysicalMeleeRigidTransform capturedPose{
        {8.0F, 9.0F, 10.0F},
        PhysicalMeleeLocalRotationFromDegrees({11.0F, 12.0F, 13.0F})};
    assert(CaptureSlideGrabFromController(editor,capturedPose));
    assert(editor.current.configured && editor.dirty);
    assert(Near(editor.current.grabVolumeModelLocal.positionUnits.x,8.0F));
    assert(Near(editor.current.handPoseModelLocal.positionUnits.z,10.0F));
    assert(UndoSlideGrabEdit(editor) && !editor.dirty);
    editor.component=SlideGrabComponent::MaximumTravel;
    assert(AdjustSlideGrabComponent(editor,1) && editor.dirty && editor.undoCount==1);
    assert(UndoSlideGrabEdit(editor) && !editor.dirty);
    editor.component=SlideGrabComponent::Activation;
    assert(AdjustSlideGrabComponent(editor,1));
    assert(editor.current.activationInput!=colt.activationInput);
    assert(ResetSlideGrabEdit(editor) && !editor.dirty);

    for(const auto activation:{SlideGrabActivationInput::Grip,SlideGrabActivationInput::Trigger,SlideGrabActivationInput::Either}){
        auto settings=colt; settings.activationInput=activation;
        SlideGrabStateMachine machine{}; auto input=ValidFrame(settings);
        assert(UpdateSlideGrabStateMachine(machine,input).state==SlideGrabState::Candidate);
        if(activation==SlideGrabActivationInput::Grip) input.gripValue=1; else input.triggerValue=1;
        auto attached=UpdateSlideGrabStateMachine(machine,input);
        assert(attached.state==SlideGrabState::Attached && attached.requestNodeControlAttach);
        input.gripValue=input.triggerValue=0;
        auto released=UpdateSlideGrabStateMachine(machine,input);
        assert(released.state==SlideGrabState::Released && released.requestNodeControlDetach);
        assert(released.reason==SlideGrabDetachReason::InputReleased);
    }

    {
        auto settings=colt;
        settings.activationInput=SlideGrabActivationInput::Grip;
        SlideGrabStateMachine machine{};
        auto input=ValidFrame(settings);
        input.controllerModelLocal={100,100,100};
        assert(UpdateSlideGrabStateMachine(machine,input).state==
            SlideGrabState::Idle);
        input.controllerModelLocal=settings.grabVolumeModelLocal.positionUnits;
        input.gripValue=1.0F;
        const auto pressedOnEntry=UpdateSlideGrabStateMachine(machine,input);
        assert(pressedOnEntry.state==SlideGrabState::Candidate);
        assert(!pressedOnEntry.requestNodeControlAttach);
        assert(pressedOnEntry.captureGrip);
    }

    const auto cancel=[&](auto mutate,SlideGrabDetachReason reason){
        SlideGrabStateMachine machine{}; auto input=ValidFrame(colt);
        UpdateSlideGrabStateMachine(machine,input); input.gripValue=1;
        assert(UpdateSlideGrabStateMachine(machine,input).state==SlideGrabState::Attached);
        mutate(input); const auto out=UpdateSlideGrabStateMachine(machine,input);
        assert(out.state==SlideGrabState::Released && out.requestNodeControlDetach);
        assert(out.reason==reason);
    };
    cancel([](auto& v){v.trackingFresh=false;},SlideGrabDetachReason::TrackingStale);
    cancel([](auto& v){v.focused=false;},SlideGrabDetachReason::FocusLost);
    cancel([](auto& v){v.gamePlaying=false;},SlideGrabDetachReason::GameStateInvalid);
    cancel([](auto& v){v.exactWeaponIdentity=false;},SlideGrabDetachReason::WeaponChanged);
    cancel([](auto& v){++v.sourceGeneration;},SlideGrabDetachReason::SourceGenerationChanged);
    cancel([](auto& v){v.modelAvailable=false;},SlideGrabDetachReason::ModelUnavailable);
    cancel([](auto& v){v.nodeResolved=false;},SlideGrabDetachReason::NodeResolutionFailed);
    cancel([](auto& v){v.transformValid=false;},SlideGrabDetachReason::TransformInvalid);
    cancel([](auto& v){v.toolMenuOpen=true;},SlideGrabDetachReason::MenuOpened);
    cancel([](auto& v){v.retailAnimationIncompatible=true;},SlideGrabDetachReason::RetailAnimationStarted);

    {
        SlideGrabSoundCueState sound{};
        SlideGrabFrameResult frame{};
        frame.state=SlideGrabState::Attached;
        frame.requestNodeControlAttach=true;
        frame.projection.valid=true;
        assert(UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true).cue==
            SlideGrabSoundCue::None);
        frame.requestNodeControlAttach=false;
        frame.projection.clampedTravelUnits=
            colt.rearThresholdUnits-0.01F;
        frame.projection.rearReached=false;
        assert(UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true).cue==
            SlideGrabSoundCue::None);
        frame.projection.clampedTravelUnits=
            colt.rearThresholdUnits;
        frame.projection.rearReached=true;
        const auto firstPull=UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true);
        assert(firstPull.cue==SlideGrabSoundCue::Pull);
        assert(firstPull.pullCycle==1U);
        frame.projection.clampedTravelUnits=
            colt.rearThresholdUnits-0.10F;
        frame.projection.rearReached=false;
        assert(UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true).cue==
            SlideGrabSoundCue::None);
        frame.projection.clampedTravelUnits=colt.rearThresholdUnits;
        frame.projection.rearReached=true;
        assert(UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true).cue==
            SlideGrabSoundCue::None);
        frame.projection.clampedTravelUnits=2.0F;
        frame.projection.rearReached=false;
        assert(UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true).cue==
            SlideGrabSoundCue::None);
        frame.projection.clampedTravelUnits=colt.rearThresholdUnits;
        frame.projection.rearReached=true;
        const auto secondPull=UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true);
        assert(secondPull.cue==SlideGrabSoundCue::Pull);
        assert(secondPull.pullCycle==2U);
        frame.state=SlideGrabState::Released;
        frame.requestNodeControlDetach=true;
        frame.reason=SlideGrabDetachReason::InputReleased;
        const auto returned=UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true);
        assert(returned.cue==SlideGrabSoundCue::Return);
        assert(returned.pullCycle==2U);
        assert(Near(returned.travelUnits,colt.rearThresholdUnits));
    }

    {
        SlideGrabSoundCueState sound{};
        SlideGrabFrameResult frame{};
        frame.state=SlideGrabState::Attached;
        frame.requestNodeControlAttach=true;
        frame.projection.valid=true;
        UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true);
        frame.requestNodeControlAttach=false;
        frame.projection.clampedTravelUnits=1.0F;
        frame.projection.rearReached=true;
        assert(UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true).cue==
            SlideGrabSoundCue::Pull);
        frame.state=SlideGrabState::Released;
        frame.requestNodeControlDetach=true;
        frame.reason=SlideGrabDetachReason::FocusLost;
        const auto cancelled=UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true);
        assert(cancelled.cue==SlideGrabSoundCue::None);
        assert(cancelled.stopPlayback);
    }

    {
        SlideGrabSoundCueState sound{};
        SlideGrabFrameResult frame{};
        frame.state=SlideGrabState::Attached;
        frame.requestNodeControlAttach=true;
        frame.projection.valid=true;
        UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true);
        frame.requestNodeControlAttach=false;
        frame.projection.clampedTravelUnits=1.0F;
        frame.projection.rearReached=true;
        UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true);
        frame.projection.clampedTravelUnits=0.0F;
        frame.projection.rearReached=false;
        UpdateSlideGrabSoundCueState(
            sound,frame,colt.rearThresholdUnits,true);
        frame.state=SlideGrabState::Released;
        frame.requestNodeControlDetach=true;
        frame.reason=SlideGrabDetachReason::InputReleased;
        const auto alreadyClosed=
            UpdateSlideGrabSoundCueState(
                sound,frame,colt.rearThresholdUnits,true);
        assert(alreadyClosed.cue==SlideGrabSoundCue::None);
        assert(alreadyClosed.stopPlayback);
    }

    {
        SlideGrabSoundCueState sound{};
        SlideGrabFrameResult frame{};
        frame.state=SlideGrabState::Attached;
        frame.requestNodeControlAttach=true;
        frame.projection.valid=true;
        const auto rejected=
            UpdateSlideGrabSoundCueState(
                sound,frame,colt.rearThresholdUnits,false);
        assert(rejected.cue==SlideGrabSoundCue::None);
        assert(!sound.attachmentActive);
    }
    return 0;
}
