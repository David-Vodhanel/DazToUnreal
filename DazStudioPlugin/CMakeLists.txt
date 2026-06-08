set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(DZ_PLUGIN_TGT_NAME dzunrealbridge)
set(DZ_PLUGIN_PROJECT_NAME "DzBridge-Unreal")

# Daz Studio 6 needs the SDK's memory-allocator definitions compiled into the plugin.
if(DAZ6)
	set(DS6_PLUGIN_SOURCES "${DAZ_SDK_DIR}/include/dzsdkmemorydefines.cpp")
else()
	set(DS6_PLUGIN_SOURCES "")
endif()

include_directories(${COMMON_LIB_INCLUDE_DIR})

set(MLD_SOURCES
	MLDeformer.cpp
	MlDeformer.h
)

# if building a plugin and you want the compiled result placed in the Daz Studio ./plugins directory
if(DAZ_STUDIO_EXE_DIR)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${DAZ_STUDIO_EXE_DIR}/plugins)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${DAZ_STUDIO_EXE_DIR}/plugins)
endif()

if(WIN32)
	set(OS_SOURCES
		DzUnrealBridge.rc
	)
elseif(APPLE)
	set(OS_SOURCES "")
endif()

if(WIN32)
	set(QRESOURCES "Resources/resources-win.qrc")
elseif(APPLE)
	set(QRESOURCES "Resources/resources-mac.qrc")
endif()

if(DAZ6)
	# DS6 (matching the DS6 .vcxproj) defines VODS_LOCAL_BUILD, which compiles out the
	# getEmbeddedImage() icon call -- so the dpc-generated image isn't referenced and
	# we skip dpc entirely (the DS6 dpc.exe is a Qt6 app needing Qt6 DLLs on PATH).
	set(DPC_IMAGES_CPP "")
else()
	set(DPC_IMAGES_CPP "${CMAKE_CURRENT_BINARY_DIR}/DzUnrealIcon.cpp" )
	set(DPC_SRC_IMAGES
		icon.png
	)

	add_custom_command(OUTPUT ${DPC_IMAGES_CPP}
		COMMAND ${DAZ_SDK_DPC_EXE} -mode image -o ${DPC_IMAGES_CPP} ${DPC_SRC_IMAGES}
		DEPENDS ${DPC_SRC_IMAGES}
		WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
	)
endif()

add_library( ${DZ_PLUGIN_TGT_NAME} SHARED
	DzUnrealAction.cpp
	DzUnrealAction.h
	DzUnrealDialog.cpp
	DzUnrealDialog.h
	pluginmain.cpp
	version.h
	${MLD_SOURCES}
	${DPC_IMAGES_CPP}
	${OS_SOURCES}
	${QA_SRCS}
	${QRESOURCES}
	${DS6_PLUGIN_SOURCES}
)

target_include_directories(${DZ_PLUGIN_TGT_NAME}
	PUBLIC
	${FBX_SDK_INCLUDE}
	${OPENSUBDIV_INCLUDE}
	${ALEMBIC_INCLUDE}
	${IMATH_INCLUDE}
)

target_link_libraries(${DZ_PLUGIN_TGT_NAME}
	PRIVATE
	dzcore
	dzbridge-static
	${DZSDK_QT_CORE_TARGET}
	${DZSDK_QT_GUI_TARGET}
	${DZSDK_QT_SCRIPT_TARGET}
	${DZSDK_QT_NETWORK_TARGET}
	${FBX_IMPORT_LIBS}
	${OPENSUBDIV_LIB}
	${ALEMBIC_LIB}
	${IMATH_LIB}
)

if(DAZ6)
	# DS6-only link deps: the SDK allocator import lib + zlib (the DS6 dzcore no
	# longer re-exports the zlib symbols the FBX SDK needs).
	target_link_libraries(${DZ_PLUGIN_TGT_NAME} PRIVATE
		dzsdkmemory
		"${FBX_SDK_DIR}/lib/${FBX_PLATFORM}/zlib-md.lib"
	)
	# DS6 only loads plugins named dsp_*.dll (bare-named DLLs are skipped pre-load).
	set_target_properties(${DZ_PLUGIN_TGT_NAME} PROPERTIES OUTPUT_NAME "dsp_daztounreal")
	# Matches the DS6 .vcxproj: compiles out getEmbeddedImage() (the dpc icon path).
	target_compile_definitions(${DZ_PLUGIN_TGT_NAME} PRIVATE VODS_LOCAL_BUILD)
endif()


set_target_properties(${DZ_PLUGIN_TGT_NAME}
	PROPERTIES
	FOLDER ""
	PROJECT_LABEL ${DZ_PLUGIN_PROJECT_NAME}
)

target_compile_definitions(${DZ_PLUGIN_TGT_NAME}
	PUBLIC
	$<$<CONFIG:DEBUG>:UNITTEST_DZBRIDGE>
	${DZBRIDGE_LIB_FLAGS}
)

if(DPC_IMAGES_CPP)
	set_property(SOURCE ${DPC_IMAGES_CPP} PROPERTY SKIP_AUTOMOC ON)
endif()
