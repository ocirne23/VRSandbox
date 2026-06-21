Animator CharacterAnimator
	Parameter speed Float 0
	Parameter attack Trigger

	Clip idle Anim idle 
	Clip walk Anim walk
	Clip run Anim run

	BlendSpace1D locomotion speed
		Sample idle 0.0
		Sample walk 0.5

	StateMachine
		Entry Idle
		State Idle
			Play idle
		State Move
			Play locomotion
			SpeedParam speed
			SpeedScale 2.0
		State Run
			Play run
			SpeedParam speed
			SpeedScale 1.0
		State Attack
			Play attack          # one-shot; returns when it reaches ExitTime
		AnyTransition Attack
			Condition attack Trigger
			Fade 0.1
		Transition Attack Move
			ExitTime 0.9         # condition-free: fires once the attack clip is ~done
			Fade 0.2
		Transition Idle Move
			Condition speed Greater 0.1
			Fade 0.2
		Transition Move Idle
			Condition speed Less 0.1
			Fade 0.2
		Transition Move Run
			Condition speed Greater 0.5
			Fade 0.2
		Transition Run Move
			Condition speed Less 0.5
			Fade 0.2
			