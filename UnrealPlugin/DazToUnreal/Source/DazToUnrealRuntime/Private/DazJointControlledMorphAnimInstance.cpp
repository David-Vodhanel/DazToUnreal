#include "DazJointControlledMorphAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Kismet/KismetMathLibrary.h"

UDazJointControlledMorphAnimInstance::UDazJointControlledMorphAnimInstance(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

void UDazJointControlledMorphAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

}
void UDazJointControlledMorphAnimInstance::NativeUpdateAnimation(float DeltaTime)
{
	Super::NativeUpdateAnimation(DeltaTime);
	

}

FAnimInstanceProxy* UDazJointControlledMorphAnimInstance::CreateAnimInstanceProxy()
{
	return new FDazJointControlledMorphAnimInstanceProxy(this);
}

void FDazJointControlledMorphAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	Super::Initialize(InAnimInstance);
	if (UDazJointControlledMorphAnimInstance* Instance = Cast<UDazJointControlledMorphAnimInstance>(GetAnimInstanceObject()))
	{
		ControlLinks = Instance->ControlLinks;
	}
}

void FDazJointControlledMorphAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	Super::PreUpdate(InAnimInstance, DeltaSeconds);

	if (UDazJointControlledMorphAnimInstance* Instance = Cast<UDazJointControlledMorphAnimInstance>(GetAnimInstanceObject()))
	{
		if (ControlLinks.Num() != Instance->ControlLinks.Num())
		{
			ControlLinks = Instance->ControlLinks;
		}
	}
}

bool FDazJointControlledMorphAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	EvaluateAnimationNode(Output);
	for (FDazJointControlLink Link : ControlLinks)
	{
		ProcessLink(Output, Link, Link.PrimaryAxis, Link.SecondaryAxis);
	}

	return true;
}

void FDazJointControlledMorphAnimInstanceProxy::ProcessLink(FPoseContext& Output, FDazJointControlLink Link, EDazMorphAnimInstanceDriver Driver, EDazMorphAnimInstanceDriver SecondaryDriver)
{
	// Get the Local space transform and the ref pose transform to see how the transform for the source bone has changed
	const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();
	int32 BoneIndex = BoneContainer.GetPoseBoneIndexForBoneName(Link.BoneName);
	if (BoneIndex == INDEX_NONE) return;
	const FTransform& SourceRefPoseBoneTransform = BoneContainer.GetRefPoseArray()[BoneIndex];
	FCompactPoseBoneIndex CompactBoneIndex = BoneContainer.GetCompactPoseIndexFromSkeletonIndex(BoneIndex);
	//Output.Pose.
	const FTransform SourceCurrentBoneTransform = Output.Pose[CompactBoneIndex];// .GetLocalSpaceTransform(SourceBone.GetCompactPoseIndex(BoneContainer));

	float FinalDriverValue = 0.0f;
	float BoneBendAngle = ExtractSourceValue(SourceCurrentBoneTransform, SourceRefPoseBoneTransform, Driver, SecondaryDriver);
	if (Link.Keys.Num() == 0)
	{
		FinalDriverValue = BoneBendAngle * Link.Scalar;
		FinalDriverValue = FMath::Clamp(FinalDriverValue, 0.0f, 1.0f);
	}
	else
	{
		if (Link.Keys.Num() > 1)
		{
			float FirstAngle = Link.Keys[0].BoneRotation;
			float LastAngle = Link.Keys[Link.Keys.Num() - 1].BoneRotation;
			float FirstValue = Link.Keys[0].MorphAlpha;
			float LastValue = Link.Keys[Link.Keys.Num() - 1].MorphAlpha;
			FinalDriverValue = UKismetMathLibrary::MapRangeClamped(BoneBendAngle, FirstAngle, LastAngle, FirstValue, LastValue);

			for (int32 KeyIndex = 1; KeyIndex < Link.Keys.Num(); KeyIndex++)
			{
				float StartAngle = Link.Keys[KeyIndex - 1].BoneRotation;
				float StartValue = Link.Keys[KeyIndex - 1].MorphAlpha;
				float StopAngle = Link.Keys[KeyIndex].BoneRotation;
				float StopValue = Link.Keys[KeyIndex].MorphAlpha;
				float AngleRange = FMath::Abs(StopValue - StartAngle);


				if (StartAngle < BoneBendAngle && BoneBendAngle < StopAngle)
				{
					FinalDriverValue = UKismetMathLibrary::MapRangeClamped(BoneBendAngle, StartAngle, StopAngle, StartValue, StopValue);
					//FinalDriverValue = FMath::Lerp(StartValue, StopValue, (BoneBendAngle - StartAngle) / AngleRange);
				}
				if (StartAngle > BoneBendAngle && BoneBendAngle > StopAngle)
				{
					FinalDriverValue = UKismetMathLibrary::MapRangeClamped(BoneBendAngle, StartAngle, StopAngle, StartValue, StopValue);
					//FinalDriverValue = FMath::Lerp(StartValue, StopValue, (BoneBendAngle - StartAngle) / AngleRange);
				}
			}
		}
	}

	USkeleton* ProxySkeleton = Output.AnimInstanceProxy->GetSkeleton();
	SmartName::UID_Type NameUID = ProxySkeleton->GetUIDByName(USkeleton::AnimCurveMappingName, FName(Link.MorphName));
	if (NameUID != SmartName::MaxUID)
	{
		Output.Curve.Set(NameUID, FinalDriverValue);
	}
}


FDazJointControlledMorphAnimInstanceProxy::~FDazJointControlledMorphAnimInstanceProxy()
{
}

const float FDazJointControlledMorphAnimInstanceProxy::ExtractSourceValue(const FTransform& InCurrentBoneTransform, const FTransform& InRefPoseBoneTransform, EDazMorphAnimInstanceDriver Controller, EDazMorphAnimInstanceDriver SecondaryController)
{
	// Resolve source value
	float SourceValue = 0.0f;


	EDazMorphAnimInstanceRotationOrder RotationOrder = EDazMorphAnimInstanceRotationOrder::XYZ;

	if (Controller == EDazMorphAnimInstanceDriver::RotationX)
	{
		RotationOrder = EDazMorphAnimInstanceRotationOrder::YZX;
		if (SecondaryController == EDazMorphAnimInstanceDriver::RotationY)
		{
			RotationOrder = EDazMorphAnimInstanceRotationOrder::YZX;
		}
		if (SecondaryController == EDazMorphAnimInstanceDriver::RotationZ)
		{
			RotationOrder = EDazMorphAnimInstanceRotationOrder::ZYX;
		}
	}
	if (Controller == EDazMorphAnimInstanceDriver::RotationY)
	{
		RotationOrder = EDazMorphAnimInstanceRotationOrder::XZY;
		if (SecondaryController == EDazMorphAnimInstanceDriver::RotationX)
		{
			RotationOrder = EDazMorphAnimInstanceRotationOrder::XZY;
		}
		if (SecondaryController == EDazMorphAnimInstanceDriver::RotationZ)
		{
			RotationOrder = EDazMorphAnimInstanceRotationOrder::ZXY;
		}
	}
	if (Controller == EDazMorphAnimInstanceDriver::RotationZ)
	{
		RotationOrder = EDazMorphAnimInstanceRotationOrder::YXZ;
		if (SecondaryController == EDazMorphAnimInstanceDriver::RotationX)
		{
			RotationOrder = EDazMorphAnimInstanceRotationOrder::XYZ;
		}
		if (SecondaryController == EDazMorphAnimInstanceDriver::RotationY)
		{
			RotationOrder = EDazMorphAnimInstanceRotationOrder::YXZ;
		}
	}


	const FVector RotationDiff = EulerFromQuat((InCurrentBoneTransform * InRefPoseBoneTransform.Inverse()).GetRotation(), RotationOrder);
	SourceValue = RotationDiff[(int32)Controller - 1];
	if (Controller != EDazMorphAnimInstanceDriver::RotationZ)
	{
		SourceValue = SourceValue * -1.0f;
	}


	// Determine the resulting value
	float FinalDriverValue = SourceValue;
	/*if (DrivingCurve != nullptr)
	{
		// Remap thru the curve if set
		FinalDriverValue = DrivingCurve->GetFloatValue(FinalDriverValue);
	}
	else
	{
		// Apply the fixed function remapping/clamping
		if (bUseRange)
		{
			const float ClampedAlpha = FMath::Clamp(FMath::GetRangePct(RangeMin, RangeMax, FinalDriverValue), 0.0f, 1.0f);
			FinalDriverValue = FMath::Lerp(RemappedMin, RemappedMax, ClampedAlpha);
		}

		FinalDriverValue *= Multiplier;
	}*/

	return FinalDriverValue;
}

// Copied from FControlRigMathLibrary::EulerFromQuat which is in an experimental plugin
FVector FDazJointControlledMorphAnimInstanceProxy::EulerFromQuat(const FQuat& Rotation, EDazMorphAnimInstanceRotationOrder RotationOrder)
{
	float X = Rotation.X;
	float Y = Rotation.Y;
	float Z = Rotation.Z;
	float W = Rotation.W;
	float X2 = X * 2.f;
	float Y2 = Y * 2.f;
	float Z2 = Z * 2.f;
	float XX2 = X * X2;
	float XY2 = X * Y2;
	float XZ2 = X * Z2;
	float YX2 = Y * X2;
	float YY2 = Y * Y2;
	float YZ2 = Y * Z2;
	float ZX2 = Z * X2;
	float ZY2 = Z * Y2;
	float ZZ2 = Z * Z2;
	float WX2 = W * X2;
	float WY2 = W * Y2;
	float WZ2 = W * Z2;

	FVector AxisX, AxisY, AxisZ;
	AxisX.X = (1.f - (YY2 + ZZ2));
	AxisY.X = (XY2 + WZ2);
	AxisZ.X = (XZ2 - WY2);
	AxisX.Y = (XY2 - WZ2);
	AxisY.Y = (1.f - (XX2 + ZZ2));
	AxisZ.Y = (YZ2 + WX2);
	AxisX.Z = (XZ2 + WY2);
	AxisY.Z = (YZ2 - WX2);
	AxisZ.Z = (1.f - (XX2 + YY2));

	FVector Result = FVector::ZeroVector;

	if (RotationOrder == EDazMorphAnimInstanceRotationOrder::XYZ)
	{
		Result.Y = FMath::Asin(-FMath::Clamp<float>(AxisZ.X, -1.f, 1.f));

		if (FMath::Abs(AxisZ.X) < 1.f - SMALL_NUMBER)
		{
			Result.X = FMath::Atan2(AxisZ.Y, AxisZ.Z);
			Result.Z = FMath::Atan2(AxisY.X, AxisX.X);
		}
		else
		{
			Result.X = 0.f;
			Result.Z = FMath::Atan2(-AxisX.Y, AxisY.Y);
		}
	}
	else if (RotationOrder == EDazMorphAnimInstanceRotationOrder::XZY)
	{

		Result.Z = FMath::Asin(FMath::Clamp<float>(AxisY.X, -1.f, 1.f));

		if (FMath::Abs(AxisY.X) < 1.f - SMALL_NUMBER)
		{
			Result.X = FMath::Atan2(-AxisY.Z, AxisY.Y);
			Result.Y = FMath::Atan2(-AxisZ.X, AxisX.X);
		}
		else
		{
			Result.X = 0.f;
			Result.Y = FMath::Atan2(AxisX.Z, AxisZ.Z);
		}
	}
	else if (RotationOrder == EDazMorphAnimInstanceRotationOrder::YXZ)
	{
		Result.X = FMath::Asin(FMath::Clamp<float>(AxisZ.Y, -1.f, 1.f));

		if (FMath::Abs(AxisZ.Y) < 1.f - SMALL_NUMBER)
		{
			Result.Y = FMath::Atan2(-AxisZ.X, AxisZ.Z);
			Result.Z = FMath::Atan2(-AxisX.Y, AxisY.Y);
		}
		else
		{
			Result.Y = 0.f;
			Result.Z = FMath::Atan2(AxisY.X, AxisX.X);
		}
	}
	else if (RotationOrder == EDazMorphAnimInstanceRotationOrder::YZX)
	{
		Result.Z = FMath::Asin(-FMath::Clamp<float>(AxisX.Y, -1.f, 1.f));

		if (FMath::Abs(AxisX.Y) < 1.f - SMALL_NUMBER)
		{
			Result.X = FMath::Atan2(AxisZ.Y, AxisY.Y);
			Result.Y = FMath::Atan2(AxisX.Z, AxisX.X);
		}
		else
		{
			Result.X = FMath::Atan2(-AxisY.Z, AxisZ.Z);
			Result.Y = 0.f;
		}
	}
	else if (RotationOrder == EDazMorphAnimInstanceRotationOrder::ZXY)
	{
		Result.X = FMath::Asin(-FMath::Clamp<float>(AxisY.Z, -1.f, 1.f));

		if (FMath::Abs(AxisY.Z) < 1.f - SMALL_NUMBER)
		{
			Result.Y = FMath::Atan2(AxisX.Z, AxisZ.Z);
			Result.Z = FMath::Atan2(AxisY.X, AxisY.Y);
		}
		else
		{
			Result.Y = FMath::Atan2(-AxisZ.X, AxisX.X);
			Result.Z = 0.f;
		}
	}
	else if (RotationOrder == EDazMorphAnimInstanceRotationOrder::ZYX)
	{
		Result.Y = FMath::Asin(FMath::Clamp<float>(AxisX.Z, -1.f, 1.f));

		if (FMath::Abs(AxisX.Z) < 1.f - SMALL_NUMBER)
		{
			Result.X = FMath::Atan2(-AxisY.Z, AxisZ.Z);
			Result.Z = FMath::Atan2(-AxisX.Y, AxisX.X);
		}
		else
		{
			Result.X = FMath::Atan2(AxisZ.Y, AxisY.Y);
			Result.Z = 0.f;
		}
	}

	return Result * 180.f / PI;
}