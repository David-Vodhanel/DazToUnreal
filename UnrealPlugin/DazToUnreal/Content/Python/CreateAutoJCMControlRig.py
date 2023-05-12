import unreal
import argparse
import json

parser = argparse.ArgumentParser(description = 'Creates a Control Rig given a SkeletalMesh')
parser.add_argument('--skeletalMesh', help='Skeletal Mesh to Use')
parser.add_argument('--animBlueprint', help='Anim Blueprint that contains the Control Rig Node')
parser.add_argument('--dtuFile', help='DTU File to use')
args = parser.parse_args()



# Create the Control Rig
asset_name = args.skeletalMesh.split('.')[1] + '_JCM_CR'
package_path = args.skeletalMesh.rsplit('/', 1)[0]

blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name=asset_name,
                                           package_path=package_path,
                                           asset_class=unreal.ControlRigBlueprint,
                                           factory=unreal.ControlRigBlueprintFactory())

skeletal_mesh = unreal.load_object(name = args.skeletalMesh, outer = None)
unreal.ControlRigBlueprintLibrary.set_preview_mesh(blueprint, skeletal_mesh)

# Turn off notifications or each change will compile the RigVM
blueprint.suspend_notifications(True)

rig_controller = blueprint.get_controller_by_name('RigVMModel')

morph_target_names = skeletal_mesh.get_all_morph_target_names()

progress = unreal.ScopedSlowTask(len(morph_target_names), "Adding Morphs to JCM Control Rig")
progress.make_dialog()

anim_blueprint = unreal.load_object(name = args.animBlueprint, outer = None)
for graph in anim_blueprint.get_animation_graphs():
    if graph.get_fname() == 'AnimGraph':
        for graph_node in graph.get_graph_nodes_of_class(unreal.AnimGraphNode_ControlRig, True):
            anim_node = graph_node.get_editor_property('node')
            anim_node.set_editor_property('control_rig_class', blueprint.generated_class())

unreal.BlueprintEditorLibrary.compile_blueprint(anim_blueprint)        

# Setup the JCM Control Rig
hierarchy = blueprint.hierarchy
hierarchy_controller = hierarchy.get_controller()
hierarchy_controller.import_bones_from_asset(args.skeletalMesh, 'None', False, False, True)
hierarchy_controller.import_curves_from_asset(args.skeletalMesh, 'None', False)

blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_BeginExecution', 'Execute', unreal.Vector2D(-97.954082, -704.318460), 'BeginExecution')


# Get Bone list for character
dtu_data = json.load(open(args.dtuFile.replace('\"', '')))
joint_links = dtu_data['JointLinks']

joint_axis_list = {}
for joint_link in joint_links:
    joint_link_bone_name = joint_link['Bone']
    daz_axis = joint_link['Axis']
    if daz_axis == 'XRotate':
        joint_link_axis = 'YRotate'
    if daz_axis == 'YRotate':
        joint_link_axis = 'ZRotate'
    if daz_axis == 'ZRotate':
        joint_link_axis = 'XRotate'

    if not joint_link_bone_name in joint_axis_list.keys(): joint_axis_list[joint_link_bone_name] = []
    joint_axis_list[joint_link_bone_name].append(joint_link_axis)

# Go through once to get all axis for secondary axis purposes

node_height = 0
node_spacing = 250
execute_from = 'BeginExecution.ExecuteContext'
for joint_link in joint_links:
    joint_link_bone_name = joint_link['Bone']
    #if not ('Thigh' in joint_link_bone_name or 'Shin' in joint_link_bone_name): continue
    joint_link_morph = joint_link['Morph']
    joint_link_scalar = joint_link['Scalar']
    joint_link_alpha = joint_link['Alpha']

    link_name = joint_link_bone_name + '_' + joint_link_morph

    # Match Axis for Unreal
    daz_axis = joint_link['Axis']
    if daz_axis == 'XRotate':
        joint_link_primary_axis = 'YRotate'
    if daz_axis == 'YRotate':
        joint_link_primary_axis = 'ZRotate'
    if daz_axis == 'ZRotate':
        joint_link_primary_axis = 'XRotate'
    
    if joint_link_primary_axis in ['XRotate', 'ZRotate']:
        joint_link_scalar = joint_link_scalar * -1.0

    joint_min = 0.0
    if joint_link_scalar != 0.0:
        joint_max = 1.0 / joint_link_scalar
    else:
        joint_max = 90.0

    # Choose secondary axis
    joint_link_secondary_axis = None
    for axis in joint_axis_list[joint_link_bone_name]:
        if axis != joint_link_primary_axis:
            joint_link_secondary_axis = axis
            break

    joint_link_keys = []
    rotation_direction = 0
    if 'Keys' in joint_link.keys():
        for joint_link_key in joint_link['Keys']:
            key_data = {}
            key_data["Angle"] = joint_link_key['Angle']
            key_data["Value"] = joint_link_key['Value']
            joint_link_keys.append(key_data)
        
        if len(joint_link_keys) == 0 and joint_link_scalar > 0.0: rotation_direction = 1.0
        if len(joint_link_keys) == 0 and joint_link_scalar < 0.0: rotation_direction = -1.0

        for key_data in joint_link_keys:
            if key_data['Angle'] > 0.0: rotation_direction = 1.0
            if key_data['Angle'] < 0.0: rotation_direction = -1.0
            if joint_link_primary_axis in ['XRotate', 'ZRotate']:
                rotation_direction = rotation_direction * -1.0

        angles = [ abs(item['Angle']) for item in joint_link_keys ]
        angles.sort()

        joint_max = angles[-1] * rotation_direction
        joint_min = angles[-2] * rotation_direction

    rotation_order = 'ZYX'
    if joint_link_primary_axis == 'YRotate':
        rotation_order = 'ZXY'

    if joint_link_secondary_axis:
        if joint_link_primary_axis == 'XRotate' and joint_link_secondary_axis == 'YRotate':
            rotation_order = 'YZX'
        if joint_link_primary_axis == 'XRotate' and joint_link_secondary_axis == 'ZRotate':
            rotation_order = 'ZYX'
        if joint_link_primary_axis == 'YRotate' and joint_link_secondary_axis == 'XRotate':
            rotation_order = 'XZY'
        if joint_link_primary_axis == 'YRotate' and joint_link_secondary_axis == 'ZRotate':
            rotation_order = 'ZXY'
        if joint_link_primary_axis == 'ZRotate' and joint_link_secondary_axis == 'XRotate':
            rotation_order = 'XYZ'
        if joint_link_primary_axis == 'ZRotate' and joint_link_secondary_axis == 'YRotate':
            rotation_order = 'YXZ'

    joint_max = joint_max * float(joint_link_alpha)
    joint_min = joint_min * float(joint_link_alpha)

    joint_link_morphs_to_process = [joint_link_morph]
    if joint_link_morph + '_dq2lb' in morph_target_names:
        joint_link_morphs_to_process.append(joint_link_morph + '_dq2lb')

    for morph_to_process in joint_link_morphs_to_process:
        progress.enter_progress_frame()
        link_name = joint_link_bone_name + '_' + morph_to_process

        # Set Curve Value
        node_name = 'Set_' + link_name
        rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_SetCurveValue', 'Execute', unreal.Vector2D(1130.0, node_height), node_name)
        rig_controller.set_pin_default_value(node_name + '.Curve', morph_to_process, False)

        # Get Transform
        get_initial_transform_node_name = 'GetInitialTransform_' + link_name
        rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(-250.0, node_height), get_initial_transform_node_name)
        rig_controller.set_pin_default_value(get_initial_transform_node_name + '.Item', '(Type=Bone)', False, False)
        rig_controller.set_pin_expansion(get_initial_transform_node_name + '.Item', True)
        rig_controller.set_pin_default_value(get_initial_transform_node_name + '.Space', 'LocalSpace', False, False)
        rig_controller.set_pin_default_value(get_initial_transform_node_name + '.Item.Name', joint_link_bone_name, False, False)
        rig_controller.set_pin_default_value(get_initial_transform_node_name + '.Item.Type', 'Bone', False, False)
        rig_controller.set_pin_default_value(get_initial_transform_node_name + '.bInitial', 'true', False, False)

        get_transform_node_name = 'GetTransform_' + link_name
        rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(-500.0, node_height), get_transform_node_name)
        rig_controller.set_pin_default_value(get_transform_node_name + '.Item', '(Type=Bone)', False, False)
        rig_controller.set_pin_expansion(get_transform_node_name + '.Item', True)
        rig_controller.set_pin_default_value(get_transform_node_name + '.Space', 'LocalSpace', False, False)
        rig_controller.set_pin_default_value(get_transform_node_name + '.Item.Name', joint_link_bone_name, False, False)
        rig_controller.set_pin_default_value(get_transform_node_name + '.Item.Type', 'Bone', False, False)

        make_relative_node_name = 'MakeRelative_' + link_name
        rig_controller.add_template_node('Make Relative::Execute(in Global,in Parent,out Local)', unreal.Vector2D(-50.0, node_height), make_relative_node_name)
        rig_controller.add_link(get_initial_transform_node_name + '.Transform', make_relative_node_name + '.Parent')
        rig_controller.add_link(get_transform_node_name + '.Transform', make_relative_node_name + '.Global')

        # To Euler
        to_euler_node_name = 'To_Euler_' + link_name
        rig_controller.add_unit_node_from_struct_path('/Script/RigVM.RigVMFunction_MathQuaternionToEuler', 'Execute', unreal.Vector2D(276.703686, node_height), to_euler_node_name)
        rig_controller.set_pin_default_value(to_euler_node_name + '.Value', '(X=0.000000,Y=0.000000,Z=0.000000,W=1.000000)', False, False)
        rig_controller.set_pin_expansion(to_euler_node_name + '.Value', False, False)
        rig_controller.set_pin_default_value(to_euler_node_name + '.RotationOrder', rotation_order, False, False)
        rig_controller.set_pin_expansion(to_euler_node_name + '.Result', False, False)

        rig_controller.add_link(make_relative_node_name +'.Local.Rotation', to_euler_node_name + '.Value')
        #rig_controller.add_link(get_transform_node_name + '.Transform.Rotation', to_euler_node_name + '.Value')

        # Remap
        remap_node_name = 'Remap_' + link_name
        rig_controller.add_template_node('Remap::Execute(in Value,in SourceMinimum,in SourceMaximum,in TargetMinimum,in TargetMaximum,in bClamp,out Result)', unreal.Vector2D(759.666641, node_height), remap_node_name)
        rig_controller.add_link(to_euler_node_name + '.Result.' + joint_link_primary_axis[0], remap_node_name +'.Value')
        rig_controller.set_pin_default_value(remap_node_name +'.SourceMinimum', str(joint_min), False, False)
        rig_controller.set_pin_default_value(remap_node_name +'.SourceMaximum', str(joint_max), False, False)
        rig_controller.set_pin_default_value(remap_node_name +'.bClamp', 'true', False, False)
        #rig_controller.set_pin_default_value(remap_node_name +'.bClamp', 'true', False)
        
        rig_controller.add_link(remap_node_name +'.Result', node_name + '.Value')
        

        # Link to Previous
        rig_controller.add_link(execute_from, node_name + '.ExecuteContext')
        execute_from = node_name + '.ExecuteContext'
        node_height += node_spacing

blueprint.suspend_notifications(False)