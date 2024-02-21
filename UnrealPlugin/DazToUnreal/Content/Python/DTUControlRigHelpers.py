import unreal
import os.path
import json

class bone_limits_struct():
    bone_limit_x_min = 0.0
    bone_limit_x_max = 0.0
    bone_limit_y_min = 0.0
    bone_limit_y_max = 0.0
    bone_limit_z_min = 0.0
    bone_limit_z_max = 0.0

    
    def get_x_range(self):
        return abs(self.bone_limit_x_max - self.bone_limit_x_min)

    def get_y_range(self):
        return abs(self.bone_limit_y_max - self.bone_limit_y_min)

    def get_z_range(self):
        return abs(self.bone_limit_z_max - self.bone_limit_z_min)

    # For getting the preferred angle, it seems like we want the largest angle, not the biggest range
    def get_x_max_angle(self):
        return max(abs(self.bone_limit_x_max), abs(self.bone_limit_x_min))

    def get_y_max_angle(self):
        return max(abs(self.bone_limit_y_max), abs(self.bone_limit_y_min))

    def get_z_max_angle(self):
        return max(abs(self.bone_limit_z_max), abs(self.bone_limit_z_min))

    def get_preferred_angle(self):
        if(self.get_x_max_angle() > self.get_y_max_angle() and self.get_x_max_angle() > self.get_z_max_angle()):
            if abs(self.bone_limit_x_min) > abs(self.bone_limit_x_max): return self.bone_limit_x_min, 0.0, 0.0
            return self.bone_limit_x_max, 0.0, 0.0
        if(self.get_y_max_angle() > self.get_x_max_angle() and self.get_y_max_angle() > self.get_z_max_angle()):
            if abs(self.bone_limit_y_min) > abs(self.bone_limit_y_max): return 0.0, self.bone_limit_y_min, 0.0
            return 0.0, self.bone_limit_y_max, 0.0
        if(self.get_z_max_angle() > self.get_x_max_angle() and self.get_z_max_angle() > self.get_y_max_angle()):
            if abs(self.bone_limit_z_min) > abs(self.bone_limit_z_max): return 0.0, 0.0, self.bone_limit_z_min
            return 0.0, 0.0, self.bone_limit_z_max

    def get_up_vector(self):
        x, y, z = self.get_preferred_angle()
        primary_axis = unreal.Vector(x, y, z)
        primary_axis.normalize()
        up_axis = unreal.Vector(-1.0 * z, y, -1.0 * x)
        return up_axis.normal()



def get_bone_limits(dtu_json, skeletal_mesh_force_front_x):

    limits = dtu_json['LimitData']
    bone_limits_dict = {}
    for bone_limits in limits.values():
        
        bone_limits_data = bone_limits_struct()
        # Get the name of the bone
        bone_limit_name = bone_limits[0]

        # Get the bone limits
        bone_limits_data.bone_limit_x_min = bone_limits[2]
        bone_limits_data.bone_limit_x_max = bone_limits[3]
        bone_limits_data.bone_limit_y_min = bone_limits[4] * -1.0
        bone_limits_data.bone_limit_y_max = bone_limits[5] * -1.0
        bone_limits_data.bone_limit_z_min = bone_limits[6] * -1.0
        bone_limits_data.bone_limit_z_max = bone_limits[7] * -1.0

        # update the axis if force front was used (facing right)
        if skeletal_mesh_force_front_x:
            bone_limits_data.bone_limit_y_min = bone_limits[2] * -1.0
            bone_limits_data.bone_limit_y_max = bone_limits[3] * -1.0
            bone_limits_data.bone_limit_z_min = bone_limits[4] * -1.0
            bone_limits_data.bone_limit_z_max = bone_limits[5] * -1.0
            bone_limits_data.bone_limit_x_min = bone_limits[6]
            bone_limits_data.bone_limit_x_max = bone_limits[7]

        bone_limits_dict[bone_limit_name] = bone_limits_data

    return bone_limits_dict

def get_bone_limits_from_skeletalmesh(skeletal_mesh):
    asset_import_data = skeletal_mesh.get_editor_property('asset_import_data')
    fbx_path = asset_import_data.get_first_filename()
    dtu_file = fbx_path.rsplit('.', 1)[0] + '.dtu'
    dtu_file = dtu_file.replace('/UpdatedFBX/', '/')
    print(dtu_file)
    if os.path.exists(dtu_file):
        dtu_data = json.load(open(dtu_file))
        force_front_x = asset_import_data.get_editor_property('force_front_x_axis')
        bone_limits = get_bone_limits(dtu_data, force_front_x)
        return bone_limits
    return []

def get_character_type(dtu_json):
    asset_id = dtu_json['Asset Id']
    if asset_id.lower().startswith('genesis8'): return 'Genesis8'
    if asset_id.lower().startswith('genesis9'): return 'Genesis9'

    return 'Unknown'

def set_control_shape(blueprint, bone_name, shape_type):
    hierarchy = blueprint.hierarchy
    control_name = bone_name + '_ctrl'

    control_settings_root_ctrl = unreal.RigControlSettings()
    control_settings_root_ctrl.animation_type = unreal.RigControlAnimationType.ANIMATION_CONTROL
    control_settings_root_ctrl.control_type = unreal.RigControlType.EULER_TRANSFORM
    control_settings_root_ctrl.display_name = 'None'
    control_settings_root_ctrl.draw_limits = True
    control_settings_root_ctrl.shape_color = unreal.LinearColor(1.000000, 0.000000, 0.000000, 1.000000)
    
    control_settings_root_ctrl.shape_visible = True
    control_settings_root_ctrl.is_transient_control = False
    control_settings_root_ctrl.limit_enabled = [unreal.RigControlLimitEnabled(False, False), unreal.RigControlLimitEnabled(False, False), unreal.RigControlLimitEnabled(False, False), unreal.RigControlLimitEnabled(False, False), unreal.RigControlLimitEnabled(False, False), unreal.RigControlLimitEnabled(False, False), unreal.RigControlLimitEnabled(False, False), unreal.RigControlLimitEnabled(False, False), unreal.RigControlLimitEnabled(False, False)]
    control_settings_root_ctrl.minimum_value = unreal.RigHierarchy.make_control_value_from_euler_transform(unreal.EulerTransform(location=[0.000000,0.000000,0.000000],rotation=[0.000000,0.000000,0.000000],scale=[0.000000,0.000000,0.000000]))
    control_settings_root_ctrl.maximum_value = unreal.RigHierarchy.make_control_value_from_euler_transform(unreal.EulerTransform(location=[0.000000,0.000000,0.000000],rotation=[0.000000,0.000000,0.000000],scale=[0.000000,0.000000,0.000000]))
    control_settings_root_ctrl.primary_axis = unreal.RigControlAxis.X
     
    if shape_type == "root":
        control_settings_root_ctrl.shape_name = 'Square_Thick'
        hierarchy.set_control_settings(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), control_settings_root_ctrl)
        hierarchy.set_control_shape_transform(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), unreal.Transform(location=[0.000000,0.000000,0.000000],rotation=[0.000000,0.000000,0.000000],scale=[10.000000,10.000000,10.000000]), True)

    if shape_type == "hip":
        control_settings_root_ctrl.shape_name = 'Hexagon_Thick'
        hierarchy.set_control_settings(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), control_settings_root_ctrl)
        hierarchy.set_control_shape_transform(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), unreal.Transform(location=[0.000000,0.000000,0.000000],rotation=[0.000000,0.000000,0.000000],scale=[10.000000,10.000000,10.000000]), True)

    if shape_type == "iktarget":
        control_settings_root_ctrl.shape_name = 'Box_Thin'
        hierarchy.set_control_settings(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), control_settings_root_ctrl)
        #hierarchy.set_control_shape_transform(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), unreal.Transform(location=[0.000000,0.000000,0.000000],rotation=[0.000000,0.000000,0.000000],scale=[10.000000,10.000000,10.000000]), True)
    
    if shape_type == "large_2d_bend":
        control_settings_root_ctrl.shape_name = 'Arrow4_Thick'
        hierarchy.set_control_settings(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), control_settings_root_ctrl)
        hierarchy.set_control_shape_transform(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), unreal.Transform(location=[0.000000,0.000000,0.000000],rotation=[0.000000,0.000000,0.000000],scale=[8.000000,8.000000,8.000000]), True)

    if shape_type == "slider":
        control_settings_root_ctrl.control_type = unreal.RigControlType.FLOAT
        control_settings_root_ctrl.primary_axis = unreal.RigControlAxis.Y
        control_settings_root_ctrl.limit_enabled = [unreal.RigControlLimitEnabled(True, True)]
        control_settings_root_ctrl.minimum_value = unreal.RigHierarchy.make_control_value_from_float(0.000000)
        control_settings_root_ctrl.maximum_value = unreal.RigHierarchy.make_control_value_from_float(1.000000)
        control_settings_root_ctrl.shape_name = 'Arrow2_Thin'
        hierarchy.set_control_settings(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), control_settings_root_ctrl)
        hierarchy.set_control_shape_transform(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), unreal.Transform(location=[0.000000,0.000000,0.000000],rotation=[0.000000,0.000000,0.000000],scale=[0.5,0.5,0.5]), True)


last_construction_link = 'PrepareForExecution'
construction_y_pos = 200
def create_construction(blueprint, bone_name):
    global last_construction_link
    global construction_y_pos

    rig_controller = blueprint.get_controller_by_name('RigVMModel')
    if rig_controller is None:
        rig_controller = blueprint.get_controller()

    control_name = bone_name + '_ctrl'
    get_bone_transform_node_name = "RigUnit_Construction_GetTransform_" + bone_name
    set_control_transform_node_name = "RigtUnit_Construction_SetTransform_" + control_name

    rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(1000.0, construction_y_pos), get_bone_transform_node_name)
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.Item', '(Type=Bone)')
    rig_controller.set_pin_expansion(get_bone_transform_node_name + '.Item', True)
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.Space', 'GlobalSpace')
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.bInitial', 'True')
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.Item.Name', bone_name, True)
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.Item.Type', 'Bone', True)
    rig_controller.set_node_selection([get_bone_transform_node_name])
    try:
        rig_controller.add_template_node('Set Transform::Execute(in Item,in Space,in bInitial,in Value,in Weight,in bPropagateToChildren,io ExecuteContext)', unreal.Vector2D(1300.0, construction_y_pos), set_control_transform_node_name)
    except Exception as e:
        set_transform_scriptstruct = get_scriptstruct_by_node_name("SetTransform")
        rig_controller.add_unit_node(set_transform_scriptstruct, 'Execute', unreal.Vector2D(526.732236, -608.972187), set_control_transform_node_name)
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    rig_controller.set_pin_expansion(set_control_transform_node_name + '.Item', False)
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Space', 'GlobalSpace')
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.bInitial', 'True')
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Weight', '1.000000')
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.bPropagateToChildren', 'True')
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Item.Name', control_name, True)
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Item.Type', 'Control', True)
    try:
        rig_controller.add_link(get_bone_transform_node_name + '.Transform', set_control_transform_node_name + '.Value')
    except:
        try:
            rig_controller.add_link(get_bone_transform_node_name + '.Transform', set_control_transform_node_name + '.Transform')
        except Exception as e:
            print("ERROR: CreateControlRig.py, line 45: rig_controller.add_link(): " + str(e))
    #rig_controller.set_node_position_by_name(set_control_transform_node_name, unreal.Vector2D(512.000000, -656.000000))
    rig_controller.add_link(last_construction_link + '.ExecuteContext', set_control_transform_node_name + '.ExecuteContext')
    last_construction_link = set_control_transform_node_name
    construction_y_pos = construction_y_pos + 250

last_backward_solver_link = 'InverseExecution.ExecuteContext'
def create_backward_solver(blueprint, bone_name):
    global last_backward_solver_link
    control_name = bone_name + '_ctrl'
    get_bone_transform_node_name = "RigUnit_BackwardSolver_GetTransform_" + bone_name
    set_control_transform_node_name = "RigtUnit_BackwardSolver_SetTransform_" + control_name

    rig_controller = blueprint.get_controller_by_name('RigVMModel')
    if rig_controller is None:
        rig_controller = blueprint.get_controller()

    #rig_controller.add_link('InverseExecution.ExecuteContext', 'RigUnit_SetTransform_3.ExecuteContext')
    rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(-636.574629, -1370.167943), get_bone_transform_node_name)
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.Item', '(Type=Bone)')
    rig_controller.set_pin_expansion(get_bone_transform_node_name + '.Item', True)
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.Space', 'GlobalSpace')
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.Item.Name', bone_name, True)
    rig_controller.set_pin_default_value(get_bone_transform_node_name + '.Item.Type', 'Bone', True)
 
    try:
        rig_controller.add_template_node('Set Transform::Execute(in Item,in Space,in bInitial,in Value,in Weight,in bPropagateToChildren,io ExecuteContext)', unreal.Vector2D(-190.574629, -1378.167943), set_control_transform_node_name)
    except:
        set_transform_scriptstruct = get_scriptstruct_by_node_name("SetTransform")
        rig_controller.add_unit_node(set_transform_scriptstruct, 'Execute', unreal.Vector2D(-190.574629, -1378.167943), set_control_transform_node_name)
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    rig_controller.set_pin_expansion(set_control_transform_node_name + '.Item', False)
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Space', 'GlobalSpace')
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.bInitial', 'False')
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Weight', '1.000000')
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.bPropagateToChildren', 'True')
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Item.Name', control_name, True)
    rig_controller.set_pin_default_value(set_control_transform_node_name + '.Item.Type', 'Control', True)
    #rig_controller.set_pin_default_value(set_control_transform_node_name + '.Transform', '(Rotation=(X=0.000000,Y=0.000000,Z=0.000000,W=-1.000000),Translation=(X=0.551784,Y=-0.000000,Z=72.358307),Scale3D=(X=1.000000,Y=1.000000,Z=1.000000))', True)

    try:
        rig_controller.add_link(get_bone_transform_node_name + '.Transform', set_control_transform_node_name + '.Value')
    except:
        try:
            rig_controller.add_link(get_bone_transform_node_name + '.Transform', set_control_transform_node_name + '.Transform')
        except Exception as e:
            print("ERROR: CreateControlRig.py, line 84: rig_controller.add_link(): " + str(e))
    rig_controller.add_link(last_backward_solver_link, set_control_transform_node_name + '.ExecuteContext')
    last_backward_solver_link = set_control_transform_node_name + '.ExecuteContext'

def parent_control_to_control(hierarchy_controller, parent_control_name ,control_name):
    hierarchy_controller.set_parent(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=parent_control_name), False)

def parent_control_to_bone(hierarchy_controller, parent_bone_name, control_name):
    hierarchy_controller.set_parent(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), unreal.RigElementKey(type=unreal.RigElementType.BONE, name=parent_bone_name), False)

next_forward_execute = 'BeginExecution.ExecuteContext'
node_y_pos = 200.0
def create_control(blueprint, bone_name, parent_bone_name, shape_type):
    global next_forward_execute
    global node_y_pos

    hierarchy = blueprint.hierarchy
    hierarchy_controller = hierarchy.get_controller()

    control_name = bone_name + '_ctrl'
    default_setting = unreal.RigControlSettings()
    default_setting.shape_name = 'Box_Thin'
    default_setting.control_type = unreal.RigControlType.EULER_TRANSFORM
    default_value = hierarchy.make_control_value_from_euler_transform(
    unreal.EulerTransform(scale=[1, 1, 1]))

    key = unreal.RigElementKey(type=unreal.RigElementType.BONE, name=bone_name)
    hierarchy_controller.remove_element(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name))
    rig_control_element = hierarchy.find_control(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name))
    control_key = rig_control_element.get_editor_property('key')
    #print(rig_control_element)
    if control_key.get_editor_property('name') != "":
        control_key = rig_control_element.get_editor_property('key')
    else:
        try:
            control_key = hierarchy_controller.add_control(control_name, unreal.RigElementKey(), default_setting, default_value, True, True)
        except:
            control_key = hierarchy_controller.add_control(control_name, unreal.RigElementKey(), default_setting, default_value, True)
        
    #hierarchy_controller.remove_all_parents(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), True)

    transform = hierarchy.get_global_transform(key, True)
    hierarchy.set_control_offset_transform(control_key, transform, True)
    hierarchy.set_control_offset_transform(control_key, transform, False)
    #hierarchy.set_global_transform(control_key, unreal.Transform(), True)
    #hierarchy.set_global_transform(control_key, unreal.Transform(), False)
    #hierarchy.set_global_transform(control_key, transform, False)

    # if bone_name in control_list:
    #     create_direct_control(bone_name)
    # elif bone_name in effector_list:
    #     create_effector(bone_name)

    #create_direct_control(bone_name)
    create_construction(blueprint, bone_name)
    create_backward_solver(blueprint, bone_name)

    set_control_shape(blueprint, bone_name, shape_type)

    if shape_type in ['iktarget', 'large_2d_bend', 'slider']: return control_name

    # Link Control to Bone
    get_transform_node_name = bone_name + "_GetTransform"
    #
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(49.941063, node_y_pos), get_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(get_transform_node_name + '.Item', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.bInitial', 'False')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item.Name', control_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item.Type', 'Control', True)
    
    set_transform_node_name = bone_name + "_SetTransform"
    blueprint.get_controller_by_name('RigVMModel').add_template_node('Set Transform::Execute(in Item,in Space,in bInitial,in Value,in Weight,in bPropagateToChildren)', unreal.Vector2D(701.941063, node_y_pos), set_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(set_transform_node_name + '.Item', False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_transform_node_name + '.bInitial', 'False')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_transform_node_name + '.Weight', '1.000000')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_transform_node_name + '.bPropagateToChildren', 'True')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_transform_node_name + '.Item.Name', bone_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_transform_node_name + '.Item.Type', 'Bone', True)
    #blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(set_transform_node_name + '.Transform', '(Rotation=(X=0.000000,Y=0.000000,Z=0.000000,W=1.000000),Translation=(X=0.000000,Y=0.000000,Z=0.000000),Scale3D=(X=1.000000,Y=1.000000,Z=1.000000))', True)

    blueprint.get_controller_by_name('RigVMModel').add_link(get_transform_node_name + '.Transform', set_transform_node_name + '.Value')

    blueprint.get_controller_by_name('RigVMModel').add_link(next_forward_execute, set_transform_node_name + '.ExecuteContext')
    next_forward_execute = set_transform_node_name + '.ExecuteContext'

    if parent_bone_name:
        parent_control_name = parent_bone_name + '_ctrl'
        #hierarchy_controller.set_parent(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=control_name), unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=parent_control_name), True)

    node_y_pos = node_y_pos + 200

    return control_name

def create_limb_ik(blueprint, skeleton, bone_limits, root_bone_name, end_bone_name, shape_type):
    global next_forward_execute
    global node_y_pos

    end_bone_ctrl = end_bone_name + '_ctrl'

    hierarchy = blueprint.hierarchy
    hierarchy_controller = hierarchy.get_controller()
    rig_controller = blueprint.get_controller_by_name('RigVMModel')

    limb_ik_node_name = end_bone_name + '_FBIK'
    rig_controller.add_unit_node_from_struct_path('/Script/PBIK.RigUnit_PBIK', 'Execute', unreal.Vector2D(370.599976, node_y_pos), limb_ik_node_name)
    rig_controller.set_pin_default_value(limb_ik_node_name + '.Settings', '(Iterations=20,MassMultiplier=1.000000,MinMassMultiplier=0.200000,bStartSolveFromInputPose=True)')
    #rig_controller.set_pin_expansion('PBIK.Settings', False)
    #rig_controller.set_pin_default_value('PBIK.Debug', '(DrawScale=1.000000)')
    #rig_controller.set_pin_expansion('PBIK.Debug', False)
    #rig_controller.set_node_selection(['PBIK'])
    rig_controller.set_pin_default_value(limb_ik_node_name + '.Root', root_bone_name, False)
    root_FBIK_settings = rig_controller.insert_array_pin(limb_ik_node_name + '.BoneSettings', -1, '')
    rig_controller.set_pin_default_value(root_FBIK_settings + '.Bone', 'pelvis', False)
    rig_controller.set_pin_default_value(root_FBIK_settings + '.RotationStiffness', '1.0', False)
    rig_controller.set_pin_default_value(root_FBIK_settings + '.PositionStiffness', '1.0', False)

    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(limb_ik_node_name + '.Settings.RootBehavior', 'PinToInput', False)

    get_transform_node_name = end_bone_name + "_GetTransform"
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(49.941063, node_y_pos), get_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(get_transform_node_name + '.Item', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.bInitial', 'False')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item.Name', end_bone_ctrl, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item.Type', 'Control', True)

    pin_name = rig_controller.insert_array_pin(limb_ik_node_name + '.Effectors', -1, '')
    #print(pin_name)
    # rig_controller.add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(-122.733358, effector_get_transform_widget_height), get_transform_name)
    # rig_controller.set_pin_default_value(get_transform_name + '.Item', '(Type=Bone)')
    # rig_controller.set_pin_expansion(get_transform_name + '.Item', True)
    # rig_controller.set_pin_default_value(get_transform_name + '.Space', 'GlobalSpace')
    # rig_controller.set_pin_default_value(get_transform_name + '.Item.Name', control_name, True)
    # rig_controller.set_pin_default_value(get_transform_name + '.Item.Type', 'Control', True)
    # rig_controller.set_node_selection([get_transform_name])
    rig_controller.add_link(get_transform_node_name + '.Transform', pin_name + '.Transform')
    rig_controller.set_pin_default_value(pin_name + '.Bone', end_bone_name, False)
    # if(bone_name in guide_list):
    #     rig_controller.set_pin_default_value(pin_name + '.StrengthAlpha', '0.200000', False)

    # Limb Root Bone Settings
    # bone_settings_name = blueprint.get_controller_by_name('RigVMModel').insert_array_pin(limb_ik_node_name + '.BoneSettings', -1, '')
    # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(bone_settings_name + '.Bone', root_bone_name, False)
    # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(bone_settings_name + '.RotationStiffness', '1.000000', False)
    # blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(bone_settings_name + '.PositionStiffness', '1.000000', False)

    # Joint Bone Settings
    joint_bone_name = unreal.DazToUnrealBlueprintUtils.get_joint_bone(skeleton, root_bone_name, end_bone_name);
    bone_settings_name = blueprint.get_controller_by_name('RigVMModel').insert_array_pin(limb_ik_node_name + '.BoneSettings', -1, '')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(bone_settings_name + '.Bone', joint_bone_name, False)
    #blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(bone_settings_name + '.RotationStiffness', '1.000000', False)
    #blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(bone_settings_name + '.PositionStiffness', '1.000000', False)
    #print(bone_limits)
    x_preferred, y_preferred, z_preferred = bone_limits[str(joint_bone_name)].get_preferred_angle()
    # Figure out preferred angles, the primary angle is the one that turns the furthest from base pose
    rig_controller.set_pin_default_value(bone_settings_name + '.bUsePreferredAngles', 'true', False)


    rig_controller.set_pin_default_value(bone_settings_name + '.PreferredAngles.X', str(x_preferred), False)
    rig_controller.set_pin_default_value(bone_settings_name + '.PreferredAngles.Y', str(y_preferred), False)
    rig_controller.set_pin_default_value(bone_settings_name + '.PreferredAngles.Z', str(z_preferred), False)

    blueprint.get_controller_by_name('RigVMModel').add_link(next_forward_execute, limb_ik_node_name + '.ExecuteContext')
    node_y_pos = node_y_pos + 400
    next_forward_execute = limb_ik_node_name + '.ExecuteContext'

def create_2d_bend(blueprint, skeleton, bone_limits, start_bone_name, end_bone_name, shape_type):
    global node_y_pos
    global next_forward_execute
    ctrl_name = create_control(blueprint, start_bone_name, None, 'large_2d_bend')

    distribute_node_name = start_bone_name + "_to_" + end_bone_name + "_DistributeRotation"
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_DistributeRotationForItemArray', 'Execute', unreal.Vector2D(365.055382, node_y_pos), distribute_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(distribute_node_name + '.RotationEaseType', 'Linear')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(distribute_node_name + '.Weight', '0.25')

    item_collection_node_name = start_bone_name + "_to_" + end_bone_name + "_ItemCollection"
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_CollectionChainArray', 'Execute', unreal.Vector2D(120.870192, node_y_pos), item_collection_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.FirstItem', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(item_collection_node_name + '.FirstItem', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.LastItem', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(item_collection_node_name + '.LastItem', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.Reverse', 'False')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.FirstItem.Name', start_bone_name, False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.LastItem.Name', end_bone_name, False)

    blueprint.get_controller_by_name('RigVMModel').add_link(item_collection_node_name + '.Items', distribute_node_name + '.Items')

    get_transform_node_name = ctrl_name + "_2dbend_GetTransform"
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetTransform', 'Execute', unreal.Vector2D(49.941063, node_y_pos), get_transform_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(get_transform_node_name + '.Item', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Space', 'GlobalSpace')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.bInitial', 'False')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item.Name', ctrl_name, True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(get_transform_node_name + '.Item.Type', 'Control', True)

    rotation_pin = blueprint.get_controller_by_name('RigVMModel').insert_array_pin(distribute_node_name + '.Rotations', -1, '')
    print(rotation_pin)
    blueprint.get_controller_by_name('RigVMModel').add_link(get_transform_node_name + '.Transform.Rotation', rotation_pin + '.Rotation')

    #blueprint.get_controller_by_name('RigVMModel').add_link('rHand_GetTransform.Transform.Rotation', 'abdomenLower_to_chestUpper_DistributeRotation.Rotations.0.Rotation')

    blueprint.get_controller_by_name('RigVMModel').add_link(next_forward_execute, distribute_node_name + '.ExecuteContext')
    node_y_pos = node_y_pos + 350
    next_forward_execute = distribute_node_name + '.ExecuteContext'

def create_slider_bend(blueprint, skeleton, bone_limits, start_bone_name, end_bone_name, parent_control_name):
    global node_y_pos
    global next_forward_execute

    hierarchy = blueprint.hierarchy
    hierarchy_controller = hierarchy.get_controller()

    ctrl_name = create_control(blueprint, start_bone_name, None, 'slider')

    distribute_node_name = start_bone_name + "_to_" + end_bone_name + "_DistributeRotation"
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_DistributeRotationForItemArray', 'Execute', unreal.Vector2D(800.0, node_y_pos), distribute_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(distribute_node_name + '.RotationEaseType', 'Linear')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(distribute_node_name + '.Weight', '0.25')
    rotation_pin = blueprint.get_controller_by_name('RigVMModel').insert_array_pin(distribute_node_name + '.Rotations', -1, '')
    

    item_collection_node_name = start_bone_name + "_to_" + end_bone_name + "_ItemCollection"
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_CollectionChainArray', 'Execute', unreal.Vector2D(120.0, node_y_pos), item_collection_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.FirstItem', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(item_collection_node_name + '.FirstItem', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.LastItem', '(Type=Bone,Name="None")')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(item_collection_node_name + '.LastItem', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.Reverse', 'False')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.FirstItem.Name', start_bone_name, False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(item_collection_node_name + '.LastItem.Name', end_bone_name, False)


    blueprint.get_controller_by_name('RigVMModel').add_link(item_collection_node_name + '.Items', distribute_node_name + '.Items')

    # Create Rotation Node
    rotation_node_name = start_bone_name + "_to_" + end_bone_name + "_Rotation"
    x_preferred, y_preferred, z_preferred = bone_limits[start_bone_name].get_preferred_angle()
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/RigVM.RigVMFunction_MathQuaternionFromEuler', 'Execute', unreal.Vector2D(350.0, node_y_pos), rotation_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(rotation_node_name + '.Euler', '(X=0.000000,Y=0.000000,Z=0.000000)')
    blueprint.get_controller_by_name('RigVMModel').set_pin_expansion(rotation_node_name + '.Euler', True)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(rotation_node_name + '.RotationOrder', 'ZYX')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(rotation_node_name + '.Euler.X', str(x_preferred * -1.0), False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(rotation_node_name + '.Euler.Y', str(y_preferred), False)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(rotation_node_name + '.Euler.Z', str(z_preferred), False)

    # Get Control Float
    control_float_node_name = start_bone_name + "_to_" + end_bone_name + "_ControlFloat"
    blueprint.get_controller_by_name('RigVMModel').add_unit_node_from_struct_path('/Script/ControlRig.RigUnit_GetControlFloat', 'Execute', unreal.Vector2D(500.0, node_y_pos), control_float_node_name)
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(control_float_node_name + '.Control', 'None')
    blueprint.get_controller_by_name('RigVMModel').set_pin_default_value(control_float_node_name + '.Control', ctrl_name, False)

    blueprint.get_controller_by_name('RigVMModel').add_link(rotation_node_name + '.Result', rotation_pin + '.Rotation')
    blueprint.get_controller_by_name('RigVMModel').add_link(next_forward_execute, distribute_node_name + '.ExecuteContext')
    blueprint.get_controller_by_name('RigVMModel').add_link(control_float_node_name + '.FloatValue', distribute_node_name + '.Weight')


    parent_control_to_control(hierarchy_controller, parent_control_name, ctrl_name)

    # Offset the control so it's not in the figure.  Not working...
    up_vector = bone_limits[start_bone_name].get_up_vector()
    #up_transform = unreal.Transform(location=up_vector,rotation=[0.000000,0.000000,0.000000],scale=[1.000000,1.000000,1.000000])
    #up_transform = unreal.Transform(location=[0.0, 0.0, 50.0],rotation=[0.000000,0.000000,0.000000],scale=[1.000000,1.000000,1.000000])
    #hierarchy.set_control_offset_transform(unreal.RigElementKey(type=unreal.RigElementType.CONTROL, name=ctrl_name), up_transform)


    node_y_pos = node_y_pos + 350
    next_forward_execute = distribute_node_name + '.ExecuteContext'