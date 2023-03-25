import unreal
import argparse
import json

last_construction_link = 'PrepareForExecution'
def create_construction(bone_name):
    global last_construction_link
    control_name = bone_name + '_ctrl'
    get_bone_transform_node_name = "RigUnit_Construction_GetTransform_" + bone_name
    set_control_transform_node_name = "RigtUnit_Construction_SetTransform_" + control_name

    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(137.732236, -595.972187), get_bone_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.Item', '(Type=Bone)')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(get_bone_transform_node_name + '.Item', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.bInitial', 'True')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.Item.Name', bone_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.Item.Type', 'Bone', True)
    blueprint.get_controller_by_name('RigVMModel').set_node_selection([get_bone_transform_node_name])
    blueprint.get_controller_by_name('RigVMModel').add_template_node('Set Transform::Execute(in Item,in Space,in bInitial,in Value,in Weight,in bPropagateToChildren,io ExecuteContext)', unreal.Vector2D(526.732236, -608.972187), set_control_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(set_control_transform_node_name + '.Item', False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.bInitial', 'True')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Weight', '1.000000')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.bPropagateToChildren', 'True')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Item.Name', control_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Item.Type', 'Control', True)
    blueprint.get_controller_by_name('RigVMModel').add_link(get_bone_transform_node_name + '.Transform', set_control_transform_node_name + '.Value')
    blueprint.get_controller_by_name('RigVMModel').set_node_position_by_name(set_control_transform_node_name, unreal.Vector2D(512.000000, -656.000000))

    blueprint.get_controller_by_name('RigVMModel').add_link(last_construction_link + '.ExecuteContext', set_control_transform_node_name + '.ExecuteContext')
    last_construction_link = set_control_transform_node_name

last_backward_solver_link = 'InverseExecution.ExecuteContext'
def create_backward_solver(bone_name):
    global last_backward_solver_link
    control_name = bone_name + '_ctrl'
    get_bone_transform_node_name = "RigUnit_BackwardSolver_GetTransform_" + bone_name
    set_control_transform_node_name = "RigtUnit_BackwardSolver_SetTransform_" + control_name

    #blueprint.get_controller_by_name('RigVMModel').add_link('InverseExecution.ExecuteContext', 'RigUnit_SetTransform_3.ExecuteContext')
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(-636.574629, -1370.167943), get_bone_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.Item', '(Type=Bone)')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(get_bone_transform_node_name + '.Item', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.Item.Name', bone_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_bone_transform_node_name + '.Item.Type', 'Bone', True)
 
    blueprint.get_controller_by_name('RigVMModel').add_template_node('Set Transform::Execute(in Item,in Space,in bInitial,in Value,in Weight,in bPropagateToChildren,io ExecuteContext)', unreal.Vector2D(-190.574629, -1378.167943), set_control_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(set_control_transform_node_name + '.Item', False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.bInitial', 'False')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Weight', '1.000000')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.bPropagateToChildren', 'True')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Item.Name', control_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Item.Type', 'Control', True)
    #blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_control_transform_node_name + '.Transform', '(Rotation=(X=0.000000,Y=0.000000,Z=0.000000,W=-1.000000),Translation=(X=0.551784,Y=-0.000000,Z=72.358307),Scale3D=(X=1.000000,Y=1.000000,Z=1.000000))', True)

    blueprint.get_controller_by_name('RigVMModel').add_link(get_bone_transform_node_name + '.Transform', set_control_transform_node_name + '.Value')
    blueprint.get_controller_by_name('RigVMModel').add_link(last_backward_solver_link, set_control_transform_node_name + '.ExecuteContext')
    last_backward_solver_link = set_control_transform_node_name + '.ExecuteContext'

def create_control(bone_name):
    control_name = bone_name + '_ctrl'
    default_setting = unreal.RigControlSettings()
    default_setting.shape_name = 'Box_Thin'
    default_setting.control_type = unreal.RigControlType.EULER_TRANSFORM
    default_value = hierarchy.make_control_value_from_euler_transform(
    unreal.EulerTransform(scale=[1, 1, 1]))

    key = unreal.RigElementKey(type=unreal.RigElementType.BONE, name=bone_name)
    control_key = hierarchy_controller.add_control(control_name, unreal.RigElementKey(), default_setting, default_value, True, True)
    transform = hierarchy.get_global_transform(key, True)
    hierarchy.set_control_offset_transform(control_key, transform, True)

    if bone_name in control_list:
        create_direct_control(bone_name)
    else:
        create_effector(bone_name)
    create_construction(bone_name)
    create_backward_solver(bone_name)

effector_get_transform_widget_height = 0
def create_direct_control(bone_name):
    global next_forward_execute
    global effector_get_transform_widget_height
    #effector_get_transform_widget_height += 250
    control_name = bone_name + '_ctrl'
    get_control_transform_node_name = "RigUnit_DirectControl_GetTransform_" + bone_name
    set_bone_transform_node_name = "RigtUnit_DirectControl_SetTransform_" + control_name
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(101.499447, -244.500249), get_control_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_control_transform_node_name + '.Item', '(Type=Bone)')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(get_control_transform_node_name + '.Item', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_control_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_control_transform_node_name + '.Item.Name', control_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_control_transform_node_name + '.Item.Type', 'Control', True)

    blueprint.get_controller_by_name('RigVMModel').add_template_node('Set Transform::Execute(in Item,in Space,in bInitial,in Value,in Weight,in bPropagateToChildren,io ExecuteContext)', unreal.Vector2D(542.832780, -257.833582), set_bone_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_bone_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(set_bone_transform_node_name + '.Item', False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_bone_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_bone_transform_node_name + '.bInitial', 'False')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_bone_transform_node_name + '.Weight', '1.000000')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_bone_transform_node_name + '.bPropagateToChildren', 'True')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_bone_transform_node_name + '.Item.Name', bone_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_bone_transform_node_name + '.Item.Type', 'Bone', True)

    blueprint.get_controller_by_name('RigVMModel').add_link(get_control_transform_node_name + '.Transform', set_bone_transform_node_name + '.Value')
    blueprint.get_controller_by_name('RigVMModel').add_link(next_forward_execute, set_bone_transform_node_name + '.ExecuteContext')
    next_forward_execute = set_bone_transform_node_name + '.ExecuteContext'
   
def create_effector(bone_name):
    global effector_get_transform_widget_height
    effector_get_transform_widget_height += 250
    control_name = bone_name + '_ctrl'
    get_transform_name = control_name + '_RigUnit_GetTransform'
    pin_name = blueprint.get_controller_by_name('RigVMModel').insert_array_pin('PBIK.Effectors', -1, '')
    print(pin_name)
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(-122.733358, effector_get_transform_widget_height), get_transform_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_name + '.Item', '(Type=Bone)')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(get_transform_name + '.Item', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_name + '.Item.Name', control_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_name + '.Item.Type', 'Control', True)
    blueprint.get_controller_by_name('RigVMModel').set_node_selection([get_transform_name])
    blueprint.get_controller_by_name('RigVMModel').add_link(get_transform_name + '.Transform', pin_name + '.Transform')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(pin_name + '.Bone', bone_name, False)
    if(bone_name in guide_list):
        blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(pin_name + '.StrengthAlpha', '0.200000', False)


parser = argparse.ArgumentParser(description = 'Creates a Control Rig given a SkeletalMesh')
parser.add_argument('--skeletalMesh', help='Skeletal Mesh to Use')
parser.add_argument('--dtuFile', help='DTU File to use')
args = parser.parse_args()

asset_name = args.skeletalMesh.split('.')[1] + '_CR'
package_path = args.skeletalMesh.rsplit('/', 1)[0]

blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name=asset_name,
                                           package_path=package_path,
                                           asset_class=unreal.ControlRigBlueprint,
                                           factory=unreal.ControlRigBlueprintFactory())

#blueprint = unreal.load_object(name = '/Game/NewControlRigBlueprint.NewControlRigBlueprint', outer = None)

if blueprint: 
    library = blueprint.get_local_function_library()
    library_controller = blueprint.get_controller(library)
    hierarchy = blueprint.hierarchy
    hierarchy_controller = hierarchy.get_controller()
    blueprint.get_controller_by_name('RigVMModel').set_node_selection(['RigUnit_BeginExecution'])
    hierarchy_controller.import_bones_from_asset(args.skeletalMesh, 'None', False, False, True)

    # Create Full Body IK Node
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_BeginExecution', 'Execute', unreal.Vector2D(22.229613, 60.424645), 'BeginExecution')
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_PrepareForExecution', 'Execute', unreal.Vector2D(-216.659278, -515.130927), 'PrepareForExecution')
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_InverseExecution', 'Execute', unreal.Vector2D(-307.389434, -270.395477), 'InverseExecution')
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/PBIK.RigUnit_PBIK', 'Execute', unreal.Vector2D(370.599976, 42.845032), 'PBIK')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.Settings', '(Iterations=20,MassMultiplier=1.000000,MinMassMultiplier=0.200000,bStartSolveFromInputPose=True)')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion('PBIK.Settings', False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.Debug', '(DrawScale=1.000000)')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion('PBIK.Debug', False)
    blueprint.get_controller_by_name('RigVMModel').set_node_selection(['PBIK'])
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.Root', 'hip', False)
    blueprint.get_controller_by_name('RigVMModel').insert_array_pin('PBIK.BoneSettings', -1, '')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.BoneSettings.0.Bone', 'pelvis', False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.BoneSettings.0.RotationStiffness', '0.900000', False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.BoneSettings.0.PositionStiffness', '0.900000', False)

    next_forward_execute = 'BeginExecution.ExecuteContext'
    #blueprint.get_controller_by_name('RigVMModel').add_link('BeginExecution.ExecuteContext', 'PBIK.ExecuteContext')

    # Get Bone list for character
    dtu_data = json.load(open(args.dtuFile.replace('\"', '')))
    limits = dtu_data['LimitData']
    bones_in_dtu = ['root']
    for bone_limits in limits.values():
        bone_limit_name = bone_limits[0]
        print(bone_limit_name)
        bones_in_dtu.append(bone_limit_name)

    # Create Effectors, order matters.  Child controls should be after Parent Control
    effector_list = ['pelvis', 'l_foot', 'r_foot', 'spine4', 'l_hand', 'r_hand'] # G9
    effector_list+= ['chestUpper', 'head', 'lFoot', 'rFoot', 'lHand', 'rHand'] #G8
    effector_list = [bone for bone in effector_list if bone in bones_in_dtu]
    print(effector_list)

    # These controls are mid chain and don't have full weight
    guide_list = ['l_shin', 'r_shin', 'l_forearm', 'r_forearm'] # G9
    guide_list+= ['lShin', 'rShin', 'lForearmBend', 'rForearmBend'] # G8
    guide_list = [bone for bone in guide_list if bone in bones_in_dtu]

    # This is for controls outside of Full Body IK
    control_list = ['root', 'hip']
    control_list = [bone for bone in control_list if bone in bones_in_dtu]

    for effector_name in control_list + effector_list + guide_list:
        create_control(effector_name)

    parent_list = {
        'root':['hip', 'l_foot', 'r_foot', 'l_shin', 'r_shin', 'lFoot', 'rFoot', 'lShin', 'rShin'],
        'hip':['pelvis'],
        'pelvis':['spine4', 'chestUpper'],
        'spine4':['head', 'l_hand', 'r_hand', 'l_forearm', 'r_forearm'],
        'chestUpper':['head', 'lHand', 'rHand', 'lForearmBend', 'rForearmBend']
    }

    for parent_bone, child_bone_list in parent_list.items():
        if not parent_bone in bones_in_dtu: continue
        filtered_child_bone_list = [bone for bone in child_bone_list if bone in bones_in_dtu]
        for child_bone in child_bone_list:
            hierarchy_controller.set_parent(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name= child_bone + '_ctrl'), unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name= parent_bone + '_ctrl'), True)
            hierarchy.set_local_transform(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=child_bone + '_ctrl'), unreal.Transform(location=[0.000000,0.000000,0.000000],rotation=[0.000000,0.000000,-0.000000],scale=[1.000000,1.000000,1.000000]), True, True)
        

    stiff_limits = ['l_shoulder', 'r_shoulder']
    stiff_limits+= ['lCollar', 'rCollar']
    exclude_limits = ['root', 'hip', 'pelvis']
    for bone_limits in limits.values():
        bone_limit_name = bone_limits[0]
        bone_limit_x_min = bone_limits[2] * -1.0
        bone_limit_x_max = bone_limits[3] * -1.0
        bone_limit_y_min = bone_limits[4] * -1.0
        bone_limit_y_max = bone_limits[5] * -1.0
        bone_limit_z_min = bone_limits[6]
        bone_limit_z_max = bone_limits[7]

        if not bone_limit_name in exclude_limits:
            #if not bone_limit_name == "l_shin": continue
            limit_bone_settings = blueprint.get_controller_by_name('RigVMModel').insert_array_pin('PBIK.BoneSettings', -1, '')
            blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Bone', bone_limit_name, False)

            y_delta = abs(bone_limit_x_max - bone_limit_x_min)
            z_delta = abs(bone_limit_y_max - bone_limit_y_min)
            x_delta = abs(bone_limit_z_max - bone_limit_z_min)

            if(x_delta < 15.0):
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.X', 'Locked', False)
            elif (x_delta > 90.0):
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.X', 'Free', False)
            else:
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.X', 'Limited', False)

            if(y_delta < 15.0):
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Y', 'Locked', False)
            elif (y_delta > 90.0):
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Y', 'Free', False)
            else:
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Y', 'Limited', False)

            if(z_delta < 15.0):
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Z', 'Locked', False)
            elif (z_delta > 90.0):
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Z', 'Free', False)
            else:
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Z', 'Limited', False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.X', 'Limited', False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Y', 'Limited', False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.Z', 'Limited', False)

            # It feels like Min\Max angel aren't the actual extents, but the amount of rotation allowed.  So they shoudl be 0 to abs(min, max)
            # I think there's a bug if a min rotation is more negative than max is positive, the negative gets clamped to the relative positive.
            blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MinX', str(min(bone_limit_z_max, bone_limit_z_min)), False)
            blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MaxX', str(max(bone_limit_z_max, abs(bone_limit_z_min))), False)
            blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MinY', str(min(bone_limit_x_max, bone_limit_x_min)), False)
            blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MaxY', str(max(bone_limit_x_max, abs(bone_limit_x_min))), False)
            blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MinZ', str(min(bone_limit_y_max, bone_limit_y_min)), False)
            blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MaxZ', str(max(bone_limit_y_max, abs(bone_limit_y_min))), False)


            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MinX', str(min(bone_limit_z_max, bone_limit_z_min)), False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MaxX', str(max(bone_limit_z_max, bone_limit_z_min)), False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MinY', str(min(bone_limit_x_max, bone_limit_x_min)), False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MaxY', str(max(bone_limit_x_max, bone_limit_x_min)), False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MinZ', str(min(bone_limit_y_max, bone_limit_y_min)), False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.MaxZ', str(max(bone_limit_y_max, bone_limit_y_min)), False)

            if bone_limit_name in stiff_limits:
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.PositionStiffness', '0.800000', False)
                blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.RotationStiffness', '0.800000', False)

            # Figure out preferred angles, the primary angle is the one that turns the furthest from base pose
            blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.bUsePreferredAngles', 'true', False)

            y_max_rotate = max(abs(bone_limit_x_min), abs(bone_limit_x_max))
            z_max_rotate = max(abs(bone_limit_y_min), abs(bone_limit_y_max))
            x_max_rotate = max(abs(bone_limit_z_min), abs(bone_limit_z_max))
            #print(bone_limit_name, x_max_rotate, y_max_rotate, z_max_rotate)

            
            limit_divider = 1.0
            if x_max_rotate > y_max_rotate and x_max_rotate > z_max_rotate:
                if abs(bone_limit_z_min) > abs(bone_limit_z_max):
                    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.PreferredAngles.X', str(bone_limit_z_min / limit_divider), False)
                else:
                    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.PreferredAngles.X', str(bone_limit_z_max / limit_divider), False)

            if y_max_rotate > x_max_rotate and y_max_rotate > z_max_rotate:
                if abs(bone_limit_x_min) > abs(bone_limit_x_max):
                    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.PreferredAngles.Y', str(bone_limit_x_min / limit_divider), False)
                else:
                    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.PreferredAngles.Y', str(bone_limit_x_max / limit_divider), False)
        
            if z_max_rotate > x_max_rotate and z_max_rotate > y_max_rotate:
                if abs(bone_limit_y_min) > abs(bone_limit_y_max):
                    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.PreferredAngles.Z', str(bone_limit_y_min / limit_divider), False)
                else:
                    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limit_bone_settings + '.PreferredAngles.Z', str(bone_limit_y_max / limit_divider), False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.BoneSettings.0.Bone', 'pelvis', False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.BoneSettings.0.RotationStiffness', '0.900000', False)
            # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value('PBIK.BoneSettings.0.PositionStiffness', '0.900000', False)
        

    # Attach the node to execute
    blueprint.get_controller_by_name('RigVMModel').add_link(next_forward_execute, 'PBIK.ExecuteContext')

    skeletal_mesh = unreal.load_object(name = args.skeletalMesh, outer = None)
    unreal.ControlRigBlueprintLibrary.set_preview_mesh(blueprint, skeletal_mesh)
    unreal.ControlRigBlueprintLibrary.recompile_vm(blueprint)
    #blueprint.get_controller_by_name('RigVMModel').add_link('RigUnit_BeginExecution.ExecuteContext', 'PBIK.ExecuteContext')