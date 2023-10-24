#include "MLDeformer.h"

#include <Alembic/Abc/All.h>
#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreOgawa/All.h>

#include <dzobject.h>
#include <dznode.h>
#include <dzbone.h>
#include <dzfigure.h>
#include <dzvertexmesh.h>
#include <dzfacetmesh.h>
#include <dzscene.h>
#include <dzmodifier.h>
#include <dzfloatproperty.h>
#include <dzintproperty.h>
#include <dzprogress.h>

void MLDeformer::GeneratePoses(DzNode* Node, int PoseCount, bool IncludeFingers, bool IncludeToes)
{
    DzProgress exportProgress = DzProgress("DazBridge: MLDeformer Creating Poses", PoseCount, false, true);
    exportProgress.setCloseOnFinish(true);
    exportProgress.enable(true);
    exportProgress.step();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);


    dzScene->setAnimRange(DzTimeRange(0, PoseCount * dzScene->getTimeStep()));
    dzScene->setPlayRange(DzTimeRange(0, PoseCount * dzScene->getTimeStep()));

    // Get the list of bones. There will be duplicates from clothing items in the list so it's a map
    QMap<QString, QList<DzNode*>> Bones;
    GetBoneList(Node, Bones, IncludeFingers, IncludeToes);

    // Get a tick size for the progress bar
    int progressTickSize = (PoseCount + 50) / 50;

    // Start at frame 1.  Leave frame 0 as the reference pose.
    for (int Frame = 1; Frame < PoseCount; Frame++)
    {
        // Need this for the UI to update, but it's very slow, so run every 100th frame.
        if (Frame % progressTickSize == 0)
        {
            exportProgress.update(Frame);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }

        for (QMap<QString, QList<DzNode*>>::iterator BoneNameIter = Bones.begin(); BoneNameIter != Bones.end(); ++BoneNameIter)
        {
            QList<DzNode*> MatchingBones = BoneNameIter.value();

            // The first bone in the list should be from the main figure.
            QList<DzNode*>::iterator BoneIter = MatchingBones.begin();

            // Find the rotation bounds for the bone
            double nXRotationMin = (*BoneIter)->getXRotControl()->getMin();
            double nXRotationMax = (*BoneIter)->getXRotControl()->getMax();
            double nYRotationMin = (*BoneIter)->getYRotControl()->getMin();
            double nYRotationMax = (*BoneIter)->getYRotControl()->getMax();
            double nZRotationMin = (*BoneIter)->getZRotControl()->getMin();
            double nZRotationMax = (*BoneIter)->getZRotControl()->getMax();

            // Create a random rotation
            double XRotation = RandomInRange(nXRotationMin, nXRotationMax);
            double YRotation = RandomInRange(nYRotationMin, nYRotationMax);
            double ZRotation = RandomInRange(nZRotationMin, nZRotationMax);

            // Set the rotation for the frame
            (*BoneIter)->getXRotControl()->setValue(dzScene->getTimeStep() * double(Frame), XRotation);
            (*BoneIter)->getYRotControl()->setValue(dzScene->getTimeStep() * double(Frame), YRotation);
            (*BoneIter)->getZRotControl()->setValue(dzScene->getTimeStep() * double(Frame), ZRotation);
        }
    }
    exportProgress.finish();
}

float MLDeformer::RandomInRange(float Min, float Max)
{
    float Random = rand() / double(RAND_MAX);
    Random = Random * (Max - Min);
    Random += Min;
    return Random;
}

void MLDeformer::GetBoneList(DzNode* Node, QMap<QString, QList<DzNode*>>& Bones, bool IncludeFingers, bool IncludeToes)
{
    if (DzBone* Bone = qobject_cast<DzBone*>(Node))
    {
        // I don't think we want to do hip rotations.
        if (Node->getName() != "hip")
        {
            // Twist bones seems to have odd ranges
            if (!Node->getName().contains("twist"))
            {
                if (!Bones.contains(Node->getName()))
                {
                    Bones.insert(Node->getName(), QList<DzNode*>());
                }
                Bones[Node->getName()].append(Node);
            }
        }

        // Not doing face bones yet
        if (Node->getName() == "head") return;

    }

    // Don't drill down if we're skipping specific sets
    if (!IncludeFingers && Node->getName() == "l_hand") return;
    if (!IncludeFingers && Node->getName() == "r_hand") return;
    if (!IncludeFingers && Node->getName() == "lHand") return;
    if (!IncludeFingers && Node->getName() == "rHand") return;

    if (!IncludeToes && Node->getName() == "l_foot") return;
    if (!IncludeToes && Node->getName() == "r_foot") return;
    if (!IncludeToes && Node->getName() == "lFoot") return;
    if (!IncludeToes && Node->getName() == "rFoot") return;
    
    // Looks through the child nodes for more bones
    for (int ChildIndex = 0; ChildIndex < Node->getNumNodeChildren(); ChildIndex++)
    {
        DzNode* ChildNode = Node->getNodeChild(ChildIndex);
        if (DzBone* ChildBone = qobject_cast<DzBone*>(ChildNode))
        {
            GetBoneList(ChildNode, Bones, IncludeFingers, IncludeToes);
        }
    }
}

void MLDeformer::ExportTrainingData(DzNode* Node, QString FileName)
{
    // Create the Abc file and set the time to match Daz output
    Alembic::AbcCoreOgawa::WriteArchive AbcWriteArchive;
    Alembic::Abc::OArchive AbcArchive = Alembic::Abc::OArchive(AbcWriteArchive, FileName.toLatin1().constData());
    Alembic::Abc::TimeSamplingPtr TimeSampling = Alembic::Abc::TimeSamplingPtr(new Alembic::Abc::TimeSampling((double)dzScene->getTimeStep() / 4800, 0.0));

    // Get a list of the figures to export (Character, Shirt, Pants, etc)
    QList<DzNode*> FigureList;
    GetFigureList(Node, FigureList);

    // Setup the progress dialog
    DzTime EndFrame = dzScene->getPlayRange().getEnd() / dzScene->getTimeStep();
    int PoseCount = FigureList.count() * EndFrame;
    DzProgress exportProgress = DzProgress("DazBridge: MLDeformer Exporting Geocache", PoseCount, false, true);
    exportProgress.setCloseOnFinish(true);
    exportProgress.enable(true);
    exportProgress.step();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  

    // Add each figure to the archive
    foreach(DzNode * FigureNode, FigureList)
    {
        
        // Get the mesh name.  Need to add a suffix or names that end with a number won't connect in Unreal
        DzObject* Object = FigureNode->getObject();
        std::string MeshName = FigureNode->getName().toLatin1().constData();
        MeshName.append("_GeoCache");

        // Create the Alembic Mesh
        Alembic::AbcGeom::OPolyMesh MeshObj(AbcArchive.getTop(), MeshName, TimeSampling);
        Alembic::AbcGeom::OPolyMeshSchema& MeshSchema = MeshObj.getSchema();

        std::map<int, int> OldVertexIndexToNewVertexIndex;
        std::vector<int> uniqueVertexIndices;
        // First pass to get vertex numbers and create a remapping
        {
            DzVertexMesh* DualQuaternionMesh = Object->getCachedGeom();
            DzFacetMesh* FacetMesh = dynamic_cast<DzFacetMesh*>(DualQuaternionMesh);

            for (int FacetIndex = 0; FacetIndex < FacetMesh->getNumFacets(); FacetIndex++)
            {
                // Add the vertex count for this face
                DzFacet Facet = FacetMesh->getFacet(FacetIndex);
                int FacetVertexCount = 3;
                if (Facet.isQuad())
                {
                    FacetVertexCount = 4;
                }

                // Add the vertex indices for this face
                for (int FacetVertexIndex = 0; FacetVertexIndex < FacetVertexCount; FacetVertexIndex++)
                {
                    if (std::find(uniqueVertexIndices.begin(), uniqueVertexIndices.end(), Facet.m_vertIdx[FacetVertexIndex]) == uniqueVertexIndices.end()) {
                        uniqueVertexIndices.push_back(Facet.m_vertIdx[FacetVertexIndex]);
                    }
                }
            }
        }

        int newIndex = 0;
        std::sort(uniqueVertexIndices.begin(), uniqueVertexIndices.end());
        for (auto iterator : uniqueVertexIndices)
        {
            int oldIndex = iterator;
            OldVertexIndexToNewVertexIndex.insert(std::pair<int, int>(oldIndex, newIndex));
            newIndex++;
        }

        // Get a tick size for the progress bar
        int progressTickSize = (PoseCount + 50) / 50;

        // Tick through the poses exporting them
        DzTimeRange PlayRange = dzScene->getPlayRange();
        for (DzTime CurrentTime = PlayRange.getStart(); CurrentTime <= PlayRange.getEnd(); CurrentTime += dzScene->getTimeStep())
        {
            // Update the frame
            DzTime Frame = CurrentTime / dzScene->getTimeStep();
            dzScene->setFrame(Frame);

            // Need this for the UI to update, but it's very slow, so run every 100th frame.
            if (Frame % progressTickSize == 0)
            {
                exportProgress.update(Frame);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            }

            // Update the character and current figure mesh for the frame
            Node->update();
            Node->finalize();
            FigureNode->update();
            FigureNode->finalize();

            // Get the Geometry
            DzVertexMesh* DualQuaternionMesh = Object->getCachedGeom();

            // Get the vertex positions
            std::vector<Imath::V3f> AlembicVertices;
            float scaleFactor = 1.0f;
            // At this point uniqueVertexIndices is a sorted list of just the used vertices.  So using this will update the indexes as they are exported
            for (auto vertexID: uniqueVertexIndices)
            {
                AlembicVertices.push_back(Imath::V3f(DualQuaternionMesh->getVertex(vertexID)[0] * scaleFactor, DualQuaternionMesh->getVertex(vertexID)[1] * scaleFactor, DualQuaternionMesh->getVertex(vertexID)[2] * scaleFactor));
            }

            // Add the vertex positions for the frame
            Alembic::AbcGeom::OPolyMeshSchema::Sample FrameSample;
            FrameSample.setPositions(Alembic::Abc::V3fArraySample(AlembicVertices));

            // Next get the vertex indexes and count for each face
            DzFacetMesh* FacetMesh = dynamic_cast<DzFacetMesh*>(DualQuaternionMesh);

            std::vector<int> faceVertexIndices;
            std::vector<int> faceVertexCounts;
            std::vector<int> uniqueVertexIndices;
            for (int FacetIndex = 0; FacetIndex < FacetMesh->getNumFacets(); FacetIndex++)
            {
                // Add the vertex count for this face
                DzFacet Facet = FacetMesh->getFacet(FacetIndex);
                int FacetVertexCount = 3;
                if (Facet.isQuad())
                {
                    FacetVertexCount = 4;
                }
                faceVertexCounts.push_back(FacetVertexCount);

                // Add the vertex indices for this face
                for (int FacetVertexIndex = 0; FacetVertexIndex < FacetVertexCount; FacetVertexIndex++)
                {
                    int vertexIndexInFace = Facet.m_vertIdx[FacetVertexIndex];
                    int convertedIndex = OldVertexIndexToNewVertexIndex[vertexIndexInFace];
                    faceVertexIndices.push_back(convertedIndex);
                }
            }

            // Add the face data for the frame
            FrameSample.setFaceIndices(Alembic::Abc::Int32ArraySample(faceVertexIndices));
            FrameSample.setFaceCounts(Alembic::Abc::Int32ArraySample(faceVertexCounts));

            // Add the frame to the Mesh
            MeshSchema.set(FrameSample);
        }
    }
    exportProgress.finish();
}

void MLDeformer::GetFigureList(DzNode* Node, QList<DzNode*>& Figures)
{
    // If this is a visible figure, add it to the list
    if (Node->isVisible())
    {
        if (DzFigure* Figure = qobject_cast<DzFigure*>(Node))
        {
            // Need interactive update to let the clothing do it's "fit" pass each frame.
            TurnOnInteractiveUpdates(Node);
            Figures.append(Node);
        }
    }

    // Looks through the child nodes for more figures
    for (int ChildIndex = 0; ChildIndex < Node->getNumNodeChildren(); ChildIndex++)
    {
        DzNode* ChildNode = Node->getNodeChild(ChildIndex);
        GetFigureList(ChildNode, Figures);
    }
}

void MLDeformer::TurnOnInteractiveUpdates(DzNode* Node)
{
    // Find the Interactive Update property and turn it On
    if (Node == nullptr) return;
    if (DzObject* Object = Node->getObject())
    {
        for (int index = 0; index < Object->getNumModifiers(); index++)
        {
            DzModifier* modifier = Object->getModifier(index);
            if (modifier)
            {
                for (int propindex = 0; propindex < modifier->getNumProperties(); propindex++)
                {
                    DzProperty* property = modifier->getProperty(propindex);
                    QString propName = property->getName();
                    QString propLabel = property->getLabel();
                    if (propName == "Interactive Update")
                    {  
                        if (DzIntProperty* numericProperty = qobject_cast<DzIntProperty*>(property))
                        {
                            numericProperty->setValue(1);
                            return;
                        }
                    }
                }

            }

        }
    
    }
}