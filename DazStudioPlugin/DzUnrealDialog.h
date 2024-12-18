#pragma once
#include "dzbasicdialog.h"
#include <QtGui/qcombobox.h>
#include <QtGui/qcheckbox.h>
#include <QtCore/qsettings.h>
#include "DzBridgeDialog.h"

class QPushButton;
class QLineEdit;
class QComboBox;
class QGroupBox;

#include "dzbridge.h"

class DzUnrealDialog : public DZ_BRIDGE_NAMESPACE::DzBridgeDialog {
	Q_OBJECT
	Q_PROPERTY(QWidget* wIntermediateFolderEdit READ getIntermediateFolderEdit)
	Q_PROPERTY(QWidget* wPortEdit READ getPortEdit)
public:
	Q_INVOKABLE QLineEdit* getIntermediateFolderEdit() { return intermediateFolderEdit; }
	Q_INVOKABLE QLineEdit* getPortEdit() { return portEdit; }
	Q_INVOKABLE QLineEdit* getMLDeformerPoseCountEdit() { return mlDeformerPoseCountEdit; }

	/** Constructor **/
	 DzUnrealDialog(QWidget *parent=nullptr);

	/** Destructor **/
	virtual ~DzUnrealDialog() {}

	// MLDeformer getters
	bool getMLDeformerIncludeFingers() {
		return mlDeformerIncludeFingersCheckBox ? mlDeformerIncludeFingersCheckBox->isChecked() : false;
	}

	bool getMLDeformerIncludeToes() {
		return mlDeformerIncludeToesCheckBox ? mlDeformerIncludeToesCheckBox->isChecked() : false;
	}

	bool getMLDeformerIncludeFace() {
		return mlDeformerIncludeFaceCheckBox ? mlDeformerIncludeFaceCheckBox->isChecked() : false;
	}

	// SkeletalMesh getters
	bool getUniqueSkeletonPerCharacter() { 
		return skeletalMeshUniqueSkeletonPerCharacterCheckBox ? skeletalMeshUniqueSkeletonPerCharacterCheckBox->isChecked() : false;
	}

	bool getConvertToEpicSkeleton() {
		return skeletalMeshConvertToEpicSkeletonCheckBox ? skeletalMeshConvertToEpicSkeletonCheckBox->isChecked() : false;
	}

	bool getFixTwistBones() {
		return skeletalMeshFixTwistBonesCheckBox ? skeletalMeshFixTwistBonesCheckBox->isChecked() : false;
	}

	bool getFaceCharacterRight() {
		return skeletalMeshFaceCharacterRightCheckBox ? skeletalMeshFaceCharacterRightCheckBox->isChecked() : false;
	}

	QString getMaterialCombineMethod() {
		return combineMaterialMethodComboBox ? combineMaterialMethodComboBox->currentText() : QString("Combine Identical");
	}

	// Settings
	Q_INVOKABLE void resetToDefaults() override;
	Q_INVOKABLE void saveSettings() override;

protected slots:
	void HandleSelectIntermediateFolderButton();
	void HandlePortChanged(const QString& port);
	void HandleTargetPluginInstallerButton() override;
	void HandleOpenIntermediateFolderButton(QString sFolderPath = "") override;
	void HandleAssetTypeComboChange(const QString& assetType) override;

protected:
	Q_INVOKABLE bool loadSavedSettings();

	QLineEdit* portEdit = nullptr;
	QLineEdit* intermediateFolderEdit = nullptr;
	QPushButton* intermediateFolderButton = nullptr;

	// MLDeformer settings
	QGroupBox* mlDeformerSettingsGroupBox = nullptr;
	QLineEdit* mlDeformerPoseCountEdit = nullptr;
	QCheckBox* mlDeformerIncludeFingersCheckBox = nullptr;
	QCheckBox* mlDeformerIncludeToesCheckBox = nullptr;
	QCheckBox* mlDeformerIncludeFaceCheckBox = nullptr;

	// SkeletalMesh settings
	QGroupBox* skeletalMeshSettingsGroupBox = nullptr;
	QCheckBox* skeletalMeshUniqueSkeletonPerCharacterCheckBox = nullptr;
	QCheckBox* skeletalMeshConvertToEpicSkeletonCheckBox = nullptr;

	// Common settings
	QGroupBox* commonSettingsGroupBox = nullptr;
	QCheckBox* skeletalMeshFixTwistBonesCheckBox = nullptr;
	QCheckBox* skeletalMeshFaceCharacterRightCheckBox = nullptr;
	QComboBox* combineMaterialMethodComboBox = nullptr;

#ifdef UNITTEST_DZBRIDGE
	friend class UnitTest_DzUnrealDialog;
#endif

};
