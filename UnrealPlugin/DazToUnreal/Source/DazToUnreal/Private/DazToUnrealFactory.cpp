#include "DazToUnrealFactory.h"
#include "DazToUnreal.h"

#include "PackageTools.h"
#include "Misc/FileHelper.h"
#include "Engine/SkeletalMesh.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "DazToUnreal"

/////////////////////////////////////////////////////
// UDazToUnrealFactory

UDazToUnrealFactory::UDazToUnrealFactory(const FObjectInitializer& ObjectInitializer)
	 : Super(ObjectInitializer)
{
	 bCreateNew = false;
	 bEditorImport = true;
	 Formats.Add(TEXT("dtu;DazToUnreal description file"));
	 SupportedClass = USkeletalMesh::StaticClass();
}


UObject* UDazToUnrealFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	 if (FPaths::FileExists(Filename))
	 {
		  FString Json;
		  FFileHelper::LoadFileToString(Json, *Filename);
		  TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(Json);
		  TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
		  if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		  {
				return FDazToUnrealModule::Get().ImportFromDaz(JsonObject, Filename);
		  }
	 }

	 return 0;
}

#undef LOCTEXT_NAMESPACE
