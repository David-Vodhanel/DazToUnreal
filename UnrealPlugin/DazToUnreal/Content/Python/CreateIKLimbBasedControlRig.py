import unreal
import argparse
import json
import DTUControlRigHelpers
import importlib
importlib.reload(DTUControlRigHelpers)

parser = argparse.ArgumentParser(description = 'Creates a Control Rig given a SkeletalMesh')
parser.add_argument('--skeletalMesh', help='Skeletal Mesh to Use')
parser.add_argument('--dtuFile', help='DTU File to use')
args = parser.parse_args()

asset_name = args.skeletalMesh.split('.')[1] + '_LimbIK_CR'
package_path = args.skeletalMesh.rsplit('/', 1)[0]

blueprint = unreal.load_object(name = package_path + '/' + asset_name , outer = None)
if not blueprint:
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name=asset_name,
                                           package_path=package_path,
                                           asset_class=unreal.ControlRigBlueprint,
                                           factory=unreal.ControlRigBlueprintFactory())

skeletal_mesh = unreal.load_object(name = args.skeletalMesh, outer = None)
skeletal_mesh_import_data = skeletal_mesh.get_editor_property('asset_import_data')
skeletal_mesh_force_front_x = skeletal_mesh_import_data.get_editor_property('force_front_x_axis')
skeleton = skeletal_mesh.get_editor_property('skeleton')
#blueprint = unreal.load_object(name = '/Game/NewControlRigBlueprint.NewControlRigBlueprint', outer = None)

if blueprint: 

    # Turn off notifications or each change will compile the RigVM
    blueprint.suspend_notifications(True)

    library = blueprint.get_local_function_library()
    library_controller = blueprint.get_controller(library)
    hierarchy = blueprint.hierarchy
    hierarchy_controller = hierarchy.get_controller()

    rig_controller = blueprint.get_controller_by_name('RigVMModel')
    if rig_controller is None:
        rig_controller = blueprint.get_controller()

    #rig_controller.set_node_selection(['RigUnit_BeginExecution'])
    hierarchy_controller.import_bones_from_asset(args.skeletalMesh, 'None', True, False, True)

    # Remove Existing Nodes
    graph = rig_controller.get_graph()
    node_count = len(graph.get_nodes())
    while node_count > 0:
        rig_controller.remove_node(graph.get_nodes()[-1])
        node_count = node_count - 1

    next_forward_execute = 'BeginExecution.ExecuteContext'

    # Create start point
    rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_BeginExecution', 'Execute', unreal.Vector2D(22.229613, 60.424645), 'BeginExecution')
    rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_PrepareForExecution', 'Execute', unreal.Vector2D(-216.659278, -515.130927), 'PrepareForExecution')
    rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_InverseExecution', 'Execute', unreal.Vector2D(-307.389434, -270.395477), 'InverseExecution')

    # Get Bone list for character
    dtu_data = json.load(open(args.dtuFile.replace('\"', '')))
    bone_limits = DTUControlRigHelpers.get_bone_limits(dtu_data, skeletal_mesh_force_front_x)
    #print(bone_limits)
    # limits = dtu_data['LimitData']
    # bones_in_dtu = ['root']
    # for bone_limits in limits.values():
    #     bone_limit_name = bone_limits[0]
    #     #print(bone_limit_name)
    #     bones_in_dtu.append(bone_limit_name)
    #print(bone_limits['lShin'])
    # Create Effectors, order matters.  Child controls should be after Parent Control
    # effector_list = ['pelvis', 'l_foot', 'r_foot', 'spine4', 'l_hand', 'r_hand'] # G9
    # effector_list+= ['chestUpper', 'head', 'lFoot', 'rFoot', 'lHand', 'rHand'] #G8
    # effector_list = [bone for bone in effector_list if bone in bones_in_dtu]
    # #print(effector_list)

    # # These controls are mid chain and don't have full weight
    # guide_list = ['l_shin', 'r_shin', 'l_forearm', 'r_forearm'] # G9
    # guide_list+= ['lShin', 'rShin', 'lForearmBend', 'rForearmBend'] # G8
    # guide_list = [bone for bone in guide_list if bone in bones_in_dtu]

    # # These controls are for bones that shouldn't move much on their own, but user can rotate them
    # suggested_rotation_list = ['l_shoulder', 'r_shoulder'] # G9
    # suggested_rotation_list+= ['lCollar', 'rCollar'] # G8
    # suggested_rotation_list = [bone for bone in suggested_rotation_list if bone in bones_in_dtu]

    # # This is for controls outside of Full Body IK
    # control_list = ['root', 'hip']
    # control_list = [bone for bone in control_list if bone in bones_in_dtu]
    if DTUControlRigHelpers.get_character_type(dtu_data) == 'Genesis8':
        DTUControlRigHelpers.create_control(blueprint, 'root', None, 'root')
        DTUControlRigHelpers.create_control(blueprint, 'hip', 'root', 'hip')
        #DTUControlRigHelpers.parent_control_to_control(hierarchy_controller, 'root_ctrl', 'hip_ctrl')

        # Spine
        DTUControlRigHelpers.create_2d_bend(blueprint, skeleton, bone_limits, 'abdomenLower', 'chestUpper', 'iktarget')
        #DTUControlRigHelpers.parent_control_to_control(hierarchy_controller, 'hip_ctrl', 'abdomenLower_ctrl')

        # Legs
        DTUControlRigHelpers.create_control(blueprint, 'lFoot', 'root', 'iktarget')
        DTUControlRigHelpers.create_limb_ik(blueprint, skeleton, bone_limits, 'pelvis', 'lFoot', 'iktarget')
        DTUControlRigHelpers.parent_control_to_control(hierarchy_controller, 'root_ctrl', 'lFoot_ctrl')   
        DTUControlRigHelpers.create_control(blueprint, 'rFoot', 'root', 'iktarget')
        DTUControlRigHelpers.create_limb_ik(blueprint, skeleton, bone_limits, 'pelvis', 'rFoot', 'iktarget')
        DTUControlRigHelpers.parent_control_to_control(hierarchy_controller, 'root_ctrl', 'rFoot_ctrl')

        # Hands
        DTUControlRigHelpers.create_control(blueprint, 'lHand', 'chestUpper', 'iktarget')
        DTUControlRigHelpers.create_limb_ik(blueprint, skeleton, bone_limits, 'lCollar', 'lHand', 'iktarget')
        DTUControlRigHelpers.parent_control_to_bone(hierarchy_controller, 'chestUpper', 'lHand_ctrl')
        DTUControlRigHelpers.create_control(blueprint, 'rHand', 'chestUpper', 'iktarget')
        DTUControlRigHelpers.create_limb_ik(blueprint, skeleton, bone_limits, 'rCollar', 'rHand', 'iktarget')
        DTUControlRigHelpers.parent_control_to_bone(hierarchy_controller, 'chestUpper', 'rHand_ctrl')

        # Fingers
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'lIndex1', 'lIndex3', 'lHand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'lMid1', 'lMid3', 'lHand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'lRing1', 'lRing3', 'lHand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'lPinky1', 'lPinky3', 'lHand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'lThumb1', 'lThumb3', 'lHand_ctrl')

        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'rIndex1', 'rIndex3', 'rHand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'rMid1', 'rMid3', 'rHand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'rRing1', 'rRing3', 'rHand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'rPinky1', 'rPinky3', 'rHand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'rThumb1', 'rThumb3', 'rHand_ctrl')

    if DTUControlRigHelpers.get_character_type(dtu_data) == 'Genesis9':
        DTUControlRigHelpers.create_control(blueprint, 'root', None, 'root')
        DTUControlRigHelpers.create_control(blueprint, 'hip', 'root', 'hip')
        #DTUControlRigHelpers.parent_control_to_control(hierarchy_controller, 'root_ctrl', 'hip_ctrl')

        # Spine
        DTUControlRigHelpers.create_2d_bend(blueprint, skeleton, bone_limits, 'spine1', 'spine4', 'iktarget')
        #DTUControlRigHelpers.parent_control_to_control(hierarchy_controller, 'hip_ctrl', 'abdomenLower_ctrl')

        # Legs
        DTUControlRigHelpers.create_control(blueprint, 'l_foot', 'root', 'iktarget')
        DTUControlRigHelpers.create_limb_ik(blueprint, skeleton, bone_limits, 'pelvis', 'l_foot', 'iktarget')
        DTUControlRigHelpers.parent_control_to_control(hierarchy_controller, 'root_ctrl', 'l_foot_ctrl')   
        DTUControlRigHelpers.create_control(blueprint, 'r_foot', 'root', 'iktarget')
        DTUControlRigHelpers.create_limb_ik(blueprint, skeleton, bone_limits, 'pelvis', 'r_foot', 'iktarget')
        DTUControlRigHelpers.parent_control_to_control(hierarchy_controller, 'root_ctrl', 'r_foot_ctrl')

        # Hands
        DTUControlRigHelpers.create_control(blueprint, 'l_hand', 'spine4', 'iktarget')
        DTUControlRigHelpers.create_limb_ik(blueprint, skeleton, bone_limits, 'l_shoulder', 'l_hand', 'iktarget')
        DTUControlRigHelpers.parent_control_to_bone(hierarchy_controller, 'spine4', 'l_hand_ctrl')
        DTUControlRigHelpers.create_control(blueprint, 'r_hand', 'spine4', 'iktarget')
        DTUControlRigHelpers.create_limb_ik(blueprint, skeleton, bone_limits, 'r_shoulder', 'r_hand', 'iktarget')
        DTUControlRigHelpers.parent_control_to_bone(hierarchy_controller, 'spine4', 'r_hand_ctrl')

        # Fingers
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'l_index1', 'l_index3', 'l_hand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'l_mid1', 'l_mid3', 'l_hand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'l_ring1', 'l_ring3', 'l_hand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'l_pinky1', 'l_pinky3', 'l_hand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'l_thumb1', 'l_thumb3', 'l_hand_ctrl')

        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'r_index1', 'r_index3', 'r_hand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'r_mid1', 'r_mid3', 'r_hand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'r_ring1', 'r_ring3', 'r_hand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'r_pinky1', 'r_pinky3', 'r_hand_ctrl')
        DTUControlRigHelpers.create_slider_bend(blueprint, skeleton, bone_limits, 'r_thumb1', 'r_thumb3', 'r_hand_ctrl')
        

    # Attach the node to execute
    #rig_controller.add_link(next_forward_execute, 'PBIK.ExecuteContext')

    unreal.ControlRigBlueprintLibrary.set_preview_mesh(blueprint, skeletal_mesh)

    # Turn on notifications and force a recompile
    blueprint.suspend_notifications(False)
    unreal.ControlRigBlueprintLibrary.recompile_vm(blueprint)
    #unreal.ControlRigBlueprintLibrary.request_control_rig_init(blueprint)
    #rig_controller.add_link('RigUnit_BeginExecution.ExecuteContext', 'PBIK.ExecuteContext')