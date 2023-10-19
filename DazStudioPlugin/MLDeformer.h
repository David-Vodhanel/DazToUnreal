#pragma once

#include "dznode.h"
#include <fbxsdk.h>

struct MLDeformerTrainingSettings
{
	
};

class MLDeformer
{
public:
	static void GeneratePoses(DzNode* Node, int PoseCount, bool IncludeFingers, bool IncludeToes);
	static void ExportTrainingData(DzNode* Node, QString FileName);

private:
	static void GetBoneList(DzNode* Node, QMap<QString, QList<DzNode*>>& Bones, bool IncludeFingers, bool IncludeToes);
	static float RandomInRange(float Min, float Max);
	static void GetFigureList(DzNode* Node, QList<DzNode*>& Figures);
	static void TurnOnInteractiveUpdates(DzNode* Node);
};