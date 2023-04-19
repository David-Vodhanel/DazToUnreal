#include <QtGui/qcheckbox.h>
#include <QtGui/QMessageBox>
#include <QtNetwork/qudpsocket.h>
#include <QtNetwork/qabstractsocket.h>
#include <QUuid.h>

#include <dzapp.h>
#include <dzscene.h>
#include <dzmainwindow.h>
#include <dzshape.h>
#include <dzproperty.h>
#include <dzobject.h>
#include <dzpresentation.h>
#include <dznumericproperty.h>
#include <dzimageproperty.h>
#include <dzcolorproperty.h>
#include <dpcimages.h>
#include <dzfigure.h>
#include <dzfacetmesh.h>
#include <dzbone.h>
#include <dzcontentmgr.h>
//#include <dznodeinstance.h>
#include "idzsceneasset.h"
#include "dzuri.h"
#include "dzprogress.h"

#include "DzUnrealAction.h"
#include "DzUnrealDialog.h"
#include "DzBridgeMorphSelectionDialog.h"
#include "DzBridgeSubdivisionDialog.h"

DzUnrealAction::DzUnrealAction() :
	 DzBridgeAction(tr("&Daz to Unreal"), tr("Send the selected node to Unreal."))
{
	 m_nPort = 0;
     m_nNonInteractiveMode = 0;
	 m_sAssetType = QString("SkeletalMesh");
	 //Setup Icon
	 QString iconName = "icon";
	 QPixmap basePixmap = QPixmap::fromImage(getEmbeddedImage(iconName.toLatin1()));
	 QIcon icon;
	 icon.addPixmap(basePixmap, QIcon::Normal, QIcon::Off);
	 QAction::setIcon(icon);

	 m_bGenerateNormalMaps = true;
}

void DzUnrealAction::executeAction()
{
	 // Check if the main window has been created yet.
	 // If it hasn't, alert the user and exit early.
	 DzMainWindow* mw = dzApp->getInterface();
	 if (!mw)
	 {
         if (m_nNonInteractiveMode == 0)
		 {
             QMessageBox::warning(0, tr("Error"),
                 tr("The main window has not been created yet."), QMessageBox::Ok);
         }
		 return;
	 }

	 // Create and show the dialog. If the user cancels, exit early,
	 // otherwise continue on and do the thing that required modal
	 // input from the user.
    if (dzScene->getNumSelectedNodes() != 1)
    {
		DzNodeList rootNodes = buildRootNodeList();
		if (rootNodes.length() == 1)
		{
			dzScene->setPrimarySelection(rootNodes[0]);
		}
		else if (rootNodes.length() > 1)
		{
			if (m_nNonInteractiveMode == 0)
			{
				QMessageBox::warning(0, tr("Error"),
					tr("Please select one Character or Prop to send."), QMessageBox::Ok);
			}
		}
    }

    // Create the dialog
	if (m_bridgeDialog == nullptr)
	{
		m_bridgeDialog = new DzUnrealDialog(mw);
	}
	else
	{
		if (m_nNonInteractiveMode == 0)
		{
			m_bridgeDialog->resetToDefaults();
			m_bridgeDialog->loadSavedSettings();
		}
	}

	// Enable autoJCM option in morph selection dialog
	if (dzScene->getPrimarySelection() != nullptr)
	{
		if (m_morphSelectionDialog == nullptr)
		{
			m_morphSelectionDialog = DZ_BRIDGE_NAMESPACE::DzBridgeMorphSelectionDialog::Get(m_bridgeDialog);
		}
		m_morphSelectionDialog->SetAutoJCMVisible(true);
	}

	// Prepare member variables when not using GUI
	if (m_nNonInteractiveMode == 1)
	{
//		if (m_sRootFolder != "") m_bridgeDialog->getIntermediateFolderEdit()->setText(m_sRootFolder);

		if (m_aMorphListOverride.isEmpty() == false)
		{
			m_bEnableMorphs = true;
			m_sMorphSelectionRule = m_aMorphListOverride.join("\n1\n");
			m_sMorphSelectionRule += "\n1\n.CTRLVS\n2\nAnything\n0";
			if (m_morphSelectionDialog == nullptr)
			{
				m_morphSelectionDialog = DZ_BRIDGE_NAMESPACE::DzBridgeMorphSelectionDialog::Get(m_bridgeDialog);
			}
			m_mMorphNameToLabel.clear();
			foreach(QString morphName, m_aMorphListOverride)
			{
				QString label = m_morphSelectionDialog->GetMorphLabelFromName(morphName);
				m_mMorphNameToLabel.insert(morphName, label);
			}
		}
		else
		{
			m_bEnableMorphs = false;
			m_sMorphSelectionRule = "";
			m_mMorphNameToLabel.clear();
		}

	}

    // If the Accept button was pressed, start the export
    int dialog_choice = -1;
	if (m_nNonInteractiveMode == 0)
	{
		dialog_choice = m_bridgeDialog->exec();
	}
    if (m_nNonInteractiveMode == 1 || dialog_choice == QDialog::Accepted)
    {
		DzProgress* exportProgress = new DzProgress("Sending to Unreal...", 10);

		// Read in Custom GUI values
		DzUnrealDialog* unrealDialog = qobject_cast<DzUnrealDialog*>(m_bridgeDialog);
		if (unrealDialog)
		{
			m_nPort = unrealDialog->getPortEdit()->text().toInt();
			m_bExportMaterialPropertiesCSV = unrealDialog->getExportMaterialPropertyCSVCheckBox()->isChecked();
		}
		// Read in Common GUI values
		readGui(m_bridgeDialog);

		exportHD(exportProgress);

		exportProgress->finish();

		// DB, 2022-June-4: Hotfix for Corrupted Imports due to UDP Packet before UpgradeToHD
		if (m_EnableSubdivisions)
		{
			QString DTUfilename = m_sDestinationPath + m_sAssetName + ".dtu";
			// Send a message to Unreal telling it to start an import
			QUdpSocket* sendSocket = new QUdpSocket(this);
			QHostAddress* sendAddress = new QHostAddress("127.0.0.1");

			sendSocket->connectToHost(*sendAddress, m_nPort);
			sendSocket->write(DTUfilename.toUtf8());
		}

		// DB 2021-09-02: messagebox "Export Complete"
		if (m_nNonInteractiveMode == 0)
		{
			QMessageBox::information(0, "DazToUnreal Bridge",
				tr("Export phase from Daz Studio complete. Please switch to Unreal to continue with Import phase."), QMessageBox::Ok);
		}

    }
}

void DzUnrealAction::writeConfiguration()
{
	if (m_pSelectedNode == nullptr)
		return;

	QTextStream* pCSVStream = nullptr;
	QFile *pCSVfile = nullptr;

	 QString DTUfilename = m_sDestinationPath + m_sExportFilename + ".dtu";
	 QFile DTUfile(DTUfilename);
	 DTUfile.open(QIODevice::WriteOnly);
	 DzJsonWriter writer(&DTUfile);
	 writer.startObject(true);

	 writeDTUHeader(writer);

	 if (m_sAssetType.toLower().contains("mesh") || m_sAssetType == "Animation")
	 {
		 if (m_bExportMaterialPropertiesCSV)
		 {
			 QString filename = m_sDestinationPath + m_sExportFilename + "_Maps.csv";
			 pCSVfile = new QFile(filename);
			 if (pCSVfile->open(QIODevice::WriteOnly))
			 {
				 pCSVStream = new QTextStream(pCSVfile);
				 *pCSVStream << "Version, Object, Material, Type, Color, Opacity, File" << endl;
			 }
		 }
		 writeAllMaterials(m_pSelectedNode, writer, pCSVStream);
		 writeAllMorphs(writer);

		 // DB, 2022-July-5: Daz To Unified Bridge Format support
		 writeMorphLinks(writer);
		 writeMorphNames(writer);
		 DzBoneList aBoneList = getAllBones(m_pSelectedNode);
		 writeSkeletonData(m_pSelectedNode, writer);
		 writeHeadTailData(m_pSelectedNode, writer);
		 writeJointOrientation(aBoneList, writer);
		 writeLimitData(aBoneList, writer);
		 writePoseData(m_pSelectedNode, writer, true);

		 writeAllSubdivisions(writer);
		 writeAllDforceInfo(m_pSelectedNode, writer);
	 }

	 if (m_sAssetType == "Pose")
	 {
		writeAllPoses(writer);
	 }

	 if (m_sAssetType == "Environment")
	 {
		 writeEnvironment(writer);
	 }

	 if (m_sAssetType == "MLDeformer")
	 {
		 writeMLDeformerData(writer);
	 }

	 writer.finishObject();
	 DTUfile.close();

	 if (pCSVStream) delete(pCSVStream);
	 if (pCSVfile) 
	 {
		pCSVfile->close();
		delete(pCSVfile);
	 }

	 // DB, 2022-June-4: Hotfix for Corrupted Imports due to UDP Packet before UpgradeToHD
	 if (m_EnableSubdivisions == false)
	 {
		 // Send a message to Unreal telling it to start an import
		 QUdpSocket* sendSocket = new QUdpSocket(this);
		 QHostAddress* sendAddress = new QHostAddress("127.0.0.1");

		 sendSocket->connectToHost(*sendAddress, m_nPort);
		 sendSocket->write(DTUfilename.toUtf8());
	 }
}

// Setup custom FBX export options
void DzUnrealAction::setExportOptions(DzFileIOSettings& ExportOptions)
{

}

// Overrides baseclass implementation with Unreal specific resets
// Resets Default Values but Ignores any saved settings
void DzUnrealAction::resetToDefaults()
{
	DzBridgeAction::resetToDefaults();

	// Must Instantiate m_bridgeDialog so that we can override any saved states
	if (m_bridgeDialog == nullptr)
	{
		DzMainWindow* mw = dzApp->getInterface();
		m_bridgeDialog = new DzUnrealDialog(mw);
	}
	m_bridgeDialog->resetToDefaults();

	if (m_subdivisionDialog != nullptr)
	{
		foreach(QObject * obj, m_subdivisionDialog->getSubdivisionCombos())
		{
			QComboBox* combo = qobject_cast<QComboBox*>(obj);
			if (combo)
				combo->setCurrentIndex(0);
		}
	}
	// reset morph selection
	//DzBridgeMorphSelectionDialog::Get(nullptr)->PrepareDialog();

}

bool DzUnrealAction::setBridgeDialog(DzBasicDialog* arg_dlg)
{
	m_bridgeDialog = qobject_cast<DzUnrealDialog*>(arg_dlg);

	if (m_bridgeDialog == nullptr && arg_dlg != nullptr)
	{
		return false;
	}

	return true;
}

QString DzUnrealAction::readGuiRootFolder()
{
	QString rootFolder = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation) + QDir::separator() + "DazToUnreal";

	if (m_bridgeDialog)
	{
		QLineEdit* intermediateFolderEdit = nullptr;
		DzUnrealDialog* unrealDialog = qobject_cast<DzUnrealDialog*>(m_bridgeDialog);

		if (unrealDialog)
			intermediateFolderEdit = unrealDialog->getIntermediateFolderEdit();

		if (intermediateFolderEdit)
			rootFolder = intermediateFolderEdit->text().replace("\\", "/");
	}
	return rootFolder;
}

#include "moc_DzUnrealAction.cpp"
