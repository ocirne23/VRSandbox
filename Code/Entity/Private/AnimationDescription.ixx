export module Entity:AnimationDescription;

import Core;
import File;

// .anm — one named animation clip descriptor: a source file + which track inside it. Clips are retargeted
// by bone name at load (see ISceneData::loadAnimations), so a rig and its animations can live in separate
// files (Mixamo-style). The clip library that uses these lives in a .apl animator.
export struct AnimationClipDesc
{
    std::string name;     // global clip name (referenced from a .apl's `Clip ... Anim <name>`)
    std::string source;   // source file path, or a registered ObjectContainer name
    std::string track;    // track within the source (empty = first kept track)
    bool loop = true;     // false = one-shot (playback clamps + holds the last frame)
    std::string skip;     // ignore tracks whose name contains this (e.g. "TPose")
    std::vector<std::pair<std::string, float>> events; // notifies: name -> normalized time (0..1)
};

// .apl — an animator graph: parameters + a clip library + blend spaces + a state machine. Maps 1:1 onto
// the Animation runtime (AnimationPlayer + AnimStateMachine + BlendSpace1D). Stored as plain data; an
// AnimatorComponent instantiates the runtime objects against a given skeleton at spawn time.
export struct AnimatorDesc
{
    enum class ParamType : uint8 { Float, Bool, Trigger };
    struct Param
    {
        std::string name;
        ParamType type = ParamType::Float;
        float floatValue = 0.0f;
        bool boolValue = false;
    };

    // A clip in this animator's library: a local name bound to a global .anm clip.
    struct ClipRef
    {
        std::string localName; // name used by states / blend-space samples within this animator
        std::string anmName;   // the .anm clip it resolves to
    };

    struct BlendSample { std::string clip; float position = 0.0f; }; // clip = a local clip name
    struct BlendSpace
    {
        std::string name;
        std::string axisParam; // the float parameter that drives the blend axis each update
        std::vector<BlendSample> samples;
    };

    struct Condition
    {
        enum class Op : uint8 { Greater, Less, Equal, Trigger };
        std::string param;
        Op op = Op::Greater;
        float value = 0.0f;     // Greater / Less threshold
        bool boolValue = false; // Equal target
    };
    // Playback-rate scaling for a state (or an animator-wide default). Either a constant Speed, or a float
    // parameter (optionally multiplied by SpeedScale) that warps the clip rate each update — e.g. driving
    // run playback from the same "speed" parameter that drives the locomotion blend.
    struct SpeedBinding
    {
        std::string param;     // float parameter driving playback speed (empty = none)
        float scale = 1.0f;    // multiplier on the parameter value
        bool hasConst = false; // a constant Speed was authored
        float value = 1.0f;    // constant playback speed (used when hasConst and no param)
        bool isSet() const { return !param.empty() || hasConst; }
    };

    struct State
    {
        std::string name;
        std::string play; // a local clip name or a blend-space name
        SpeedBinding speed;
    };
    struct Transition
    {
        std::string from; // empty = an "any-state" transition
        std::string to;
        std::vector<Condition> conditions;
        float fade = 0.2f;
        float exitTime = 0.0f;
    };
    struct StateMachine
    {
        bool present = false;
        std::string entry;
        std::vector<State> states;
        std::vector<Transition> transitions;
    };

    std::string name;
    std::vector<Param> params;
    std::vector<ClipRef> clips;
    std::vector<BlendSpace> blendSpaces;
    StateMachine stateMachine;
    SpeedBinding speed; // animator-wide playback speed default (states without their own Speed use this)
};

export bool toAnimationClipDesc(const AssetNode& node, AnimationClipDesc& out); // node.key must be "Animation"
export bool toAnimatorDesc(const AssetNode& node, AnimatorDesc& out);           // node.key must be "Animator"
