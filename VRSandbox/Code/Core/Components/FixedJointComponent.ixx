export module Components.FixedJointComponent;

export class btFixedConstraint;

export struct FixedJointComponent
{
	btFixedConstraint* pJoint = nullptr;
};