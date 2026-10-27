// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "LayoutToolPanel.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_UOBJECT");
void EmptyLinkFunctionForGeneratedCodeLayoutToolPanel() {}

// ********** Begin Cross Module References ********************************************************
COREUOBJECT_API UClass* Z_Construct_UClass_UInterface(ETypeConstructPhase);
// ********** End Cross Module References **********************************************************

// ********** Begin Same Module References *********************************************************
UPackage* Z_Construct_UPackage__Script_InNaviLayoutValidator(ETypeConstructPhase);
INNAVILAYOUTVALIDATOR_API UClass* Z_Construct_UClass_ULayoutToolPanel(ETypeConstructPhase);
INNAVILAYOUTVALIDATOR_API UClass* Z_Construct_UClass_ILayoutToolPanel(ETypeConstructPhase);
INNAVILAYOUTVALIDATOR_API UClass* Z_Construct_UClass_ULayoutToolPanel(ETypeConstructPhase);
INNAVILAYOUTVALIDATOR_API UClass* Z_Construct_UClass_ILayoutToolPanel(ETypeConstructPhase);
// ********** End Same Module References ***********************************************************
#define UHT_STRUCT_BASE(INIT) UE::CodeGen::ConstInit::TCompiledInObjectPtr<const FStructBaseChain>(UE::Private::AsStructBaseChain(INIT))

// ********** Begin Interface ULayoutToolPanel *****************************************************
#ifdef UHT_STATICS
#error UHT_STATICS already defined
#endif
#define UHT_STATICS Z_Construct_UClass_ULayoutToolPanel_Statics
struct UHT_STATICS
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Type_MetaData[] = {
		{ "ModuleRelativePath", "Public/LayoutToolPanel.h" },
	};
#endif // WITH_METADATA

// ********** Begin Interface ULayoutToolPanel constinit property declarations *********************
// ********** End Interface ULayoutToolPanel constinit property declarations ***********************
	static FTypeConstructFunc* DependentSingletons[];
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<ILayoutToolPanel>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct UHT_STATICS
FTypeConstructFunc* UHT_STATICS::DependentSingletons[] = {
	(FTypeConstructFunc*)Z_Construct_UClass_UInterface,
	(FTypeConstructFunc*)Z_Construct_UPackage__Script_InNaviLayoutValidator,
};
static_assert(UE_ARRAY_COUNT(UHT_STATICS::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams UHT_STATICS::ClassParams = {
	&Z_Construct_UClass_ULayoutToolPanel,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x000840A1u,
	METADATA_PARAMS(UE_ARRAY_COUNT(UHT_STATICS::Type_MetaData), UHT_STATICS::Type_MetaData)
};
FClassRegistrationInfo Z_Registration_Info_UClass_ULayoutToolPanel;
UClass* Z_Construct_UClass_ULayoutToolPanel(ETypeConstructPhase Phase)
{
	if (Phase == ETypeConstructPhase::Inner)
	{
		using TClass = ULayoutToolPanel;
		if (!Z_Registration_Info_UClass_ULayoutToolPanel.InnerSingleton)
		{
			GetPrivateStaticClassBody(
				TClass::StaticPackage(),
				TEXT("LayoutToolPanel"),
				Z_Registration_Info_UClass_ULayoutToolPanel.InnerSingleton,
				nullptr,
				DataSizeOf<TClass>(),
				alignof(TClass),
				TClass::StaticClassFlags,
				TClass::StaticClassCastFlags(),
				TClass::StaticConfigName(),
				(UClass::ClassConstructorType)InternalConstructor<TClass>,
				(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,
				UOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),
				&TClass::Super::StaticClass,
				&TClass::WithinClass::StaticClass
			);
		}
		return Z_Registration_Info_UClass_ULayoutToolPanel.InnerSingleton;
	}
	if (!Z_Registration_Info_UClass_ULayoutToolPanel.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_ULayoutToolPanel.OuterSingleton, UHT_STATICS::ClassParams);
	}
	return Z_Registration_Info_UClass_ULayoutToolPanel.OuterSingleton;
}
#undef UHT_STATICS
ULayoutToolPanel::ULayoutToolPanel(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, ULayoutToolPanel);
// ********** End Interface ULayoutToolPanel *******************************************************

// ********** Begin Registration *******************************************************************
#ifdef UHT_STATICS
#error UHT_STATICS already defined
#endif
#define UHT_STATICS Z_CompiledInDeferFile_FID_Users_ong_wei_song_OneDrive_Documents_GitHub_Pathfinder_InNav_InNavi_Plugins_InNaviLayoutValidator_Source_InNaviLayoutValidator_Public_LayoutToolPanel_h__Script_InNaviLayoutValidator_Statics
struct UHT_STATICS
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_ULayoutToolPanel, TEXT("ULayoutToolPanel"), &Z_Registration_Info_UClass_ULayoutToolPanel, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(ULayoutToolPanel), 3503888067U) },
	};
}; // UHT_STATICS 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_ong_wei_song_OneDrive_Documents_GitHub_Pathfinder_InNav_InNavi_Plugins_InNaviLayoutValidator_Source_InNaviLayoutValidator_Public_LayoutToolPanel_h__Script_InNaviLayoutValidator_f1692b9edeb843f332c67e64c68114bfc9681dac{
	TEXT("/Script/InNaviLayoutValidator"),
	UHT_STATICS::ClassInfo, UE_ARRAY_COUNT(UHT_STATICS::ClassInfo),
	nullptr, 0,
	nullptr, 0,
	nullptr, 0,
};
#undef UHT_STATICS
// ********** End Registration *********************************************************************
#undef UHT_STRUCT_BASE

PRAGMA_ENABLE_DEPRECATION_WARNINGS
