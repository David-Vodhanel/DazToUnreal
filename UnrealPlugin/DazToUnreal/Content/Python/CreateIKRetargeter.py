import unreal
import argparse

# Info about Python with IKRigs here:
# https://docs.unrealengine.com/5.2/en-US/using-python-to-create-and-edit-ik-rigs-in-unreal-engine/
# https://docs.unrealengine.com/5.2/en-US/using-python-to-create-and-edit-ik-retargeter-assets-in-unreal-engine/

# Figure out the forward axis for a child bone
def get_bone_forward_axis(child_bone_relative_pos):
    forward = "X+"
    if abs(child_bone_relative_pos.x) > max(abs(child_bone_relative_pos.y), abs(child_bone_relative_pos.z)): forward = 'X' + forward[1]
    if abs(child_bone_relative_pos.y) > max(abs(child_bone_relative_pos.x), abs(child_bone_relative_pos.z)): forward = 'Y' + forward[1]
    if abs(child_bone_relative_pos.z) > max(abs(child_bone_relative_pos.x), abs(child_bone_relative_pos.y)): forward = 'Z' + forward[1]

    if forward[0] == 'X' and child_bone_relative_pos.x > 0: forward = forward[0] + '+'
    if forward[0] == 'X' and child_bone_relative_pos.x < 0: forward = forward[0] + '-'
    if forward[0] == 'Y' and child_bone_relative_pos.y > 0: forward = forward[0] + '+'
    if forward[0] == 'Y' and child_bone_relative_pos.y < 0: forward = forward[0] + '-'
    if forward[0] == 'Z' and child_bone_relative_pos.z > 0: forward = forward[0] + '+'
    if forward[0] == 'Z' and child_bone_relative_pos.z < 0: forward = forward[0] + '-'
    return forward

parser = argparse.ArgumentParser(description = 'Creates an IKRetargetter between two control rigs')
parser.add_argument('--sourceIKRig', help='Source IKRig to Use')
parser.add_argument('--targetIKRig', help='Target IKRig to Use')
args = parser.parse_args()

# Retageter name is Source_to_Target
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
source_rig_name = args.sourceIKRig.split('.')[-1]
target_rig_name = args.targetIKRig.split('.')[-1]

# Check if it exists
package_path = args.targetIKRig.rsplit('/', 1)[0] + '/'
asset_name = source_rig_name.replace('_IKRig', '') + '_to_' + target_rig_name.replace('_IKRig', '') + '_IKRetargeter'
rtg = skel_mesh = unreal.load_asset(name = package_path + asset_name )

# If not, create a new one
if not rtg:
    rtg = asset_tools.create_asset( asset_name=asset_name,
                                    package_path=package_path, 
                                    asset_class=unreal.IKRetargeter, 
                                    factory=unreal.IKRetargetFactory())

# Get the IK Retargeter controller
rtg_controller = unreal.IKRetargeterController.get_controller(rtg)

# Load the Source and Target IK Rigs
source_ik_rig = unreal.load_asset(name = args.sourceIKRig)
target_ik_rig = unreal.load_asset(name = args.targetIKRig)

source_ikr_controller = unreal.IKRigController.get_controller(source_ik_rig)
target_ikr_controller = unreal.IKRigController.get_controller(target_ik_rig)

# Load the Source and Target  Skeletal Meshes
source_ik_mesh = unreal.IKRigController.get_controller(source_ik_rig).get_skeletal_mesh()
target_ik_mesh = unreal.IKRigController.get_controller(target_ik_rig).get_skeletal_mesh()

# Assign the Source and Target IK Rigs
rtg_controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_ik_rig)
rtg_controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_ik_rig)

rtg_controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)

# Get the root rotation
source_asset_import_data = source_ik_mesh.get_editor_property('asset_import_data')
target_asset_import_data = target_ik_mesh.get_editor_property('asset_import_data')

source_force_front_x = source_asset_import_data.get_editor_property('force_front_x_axis')
target_force_front_x = target_asset_import_data.get_editor_property('force_front_x_axis')

# Rotate the root if needed so the character faces forward
if source_force_front_x == False and target_force_front_x == True:
    rotation_offset = unreal.Rotator()
    rotation_offset.yaw = 90
    rtg_controller.set_rotation_offset_for_retarget_pose_bone("root", rotation_offset.quaternion(), unreal.RetargetSourceOrTarget.TARGET)

# Setup the root translation
root_chain_settings = rtg_controller.get_retarget_chain_settings('Root')
root_chain_fk_settings = root_chain_settings.get_editor_property('fk')
root_chain_fk_settings.set_editor_property('translation_mode', unreal.RetargetTranslationMode.ABSOLUTE)
rtg_controller.set_retarget_chain_settings('Root', root_chain_settings)

# Align chains
source_skeleton = source_ik_mesh.get_editor_property('skeleton')
target_skeleton = target_ik_mesh.get_editor_property('skeleton')

source_reference_pose = source_skeleton.get_reference_pose()
target_reference_pose = target_skeleton.get_reference_pose()

for chain in ['LeftArm', 'RightArm', 'LeftLeg', 'RightLeg']:

    # Get the source chain info
    source_start_bone = source_ikr_controller.get_retarget_chain_start_bone(chain)
    source_end_bone = unreal.DazToUnrealBlueprintUtils.get_child_bone(source_skeleton, source_start_bone)
    source_start_bone_transform = source_reference_pose.get_ref_bone_pose(source_start_bone, space=unreal.AnimPoseSpaces.WORLD)
    source_end_bone_transform = source_reference_pose.get_ref_bone_pose(source_end_bone, space=unreal.AnimPoseSpaces.WORLD)
    source_relative_position = source_end_bone_transform.get_editor_property('translation') - source_start_bone_transform.get_editor_property('translation')

    # Fix the axis if the target character is facing right
    if source_force_front_x == False and target_force_front_x == True:
        new_relative_position = unreal.Vector()
        new_relative_position.x = source_relative_position.y * -1.0
        new_relative_position.y = source_relative_position.x * -1.0
        new_relative_position.z = source_relative_position.z
        source_relative_position = new_relative_position

    # Get the target chain info
    target_start_bone = target_ikr_controller.get_retarget_chain_start_bone(chain)
    target_end_bone = unreal.DazToUnrealBlueprintUtils.get_child_bone(target_skeleton, target_start_bone)
    target_start_bone_transform_world = target_reference_pose.get_ref_bone_pose(target_start_bone, space=unreal.AnimPoseSpaces.WORLD)
    target_end_bone_transform_local = target_reference_pose.get_ref_bone_pose(target_end_bone, space=unreal.AnimPoseSpaces.LOCAL)
    target_position = target_start_bone_transform_world.get_editor_property('translation') + source_relative_position
    
    # Find the forward direction for the 
    forward_direction = get_bone_forward_axis(target_end_bone_transform_local.get_editor_property('translation'))

    # The new target for the joint rotation
    new_pos = target_start_bone_transform_world.inverse_transform_location(target_position)

    # Make a rotator to point the joints at the new
    new_rot = unreal.Rotator()
    if forward_direction[0] == 'X':
        y_rot = unreal.MathLibrary.deg_atan(new_pos.z / new_pos.x) * -1.0
        z_rot = unreal.MathLibrary.deg_atan(new_pos.y / new_pos.x) 
        new_rot.set_editor_property('pitch', y_rot) #Y
        new_rot.set_editor_property('yaw', z_rot) #Z

    if forward_direction[0] == 'Y':
        x_rot = unreal.MathLibrary.deg_atan(new_pos.z / new_pos.y) * -1.0
        z_rot = unreal.MathLibrary.deg_atan(new_pos.x / new_pos.y) * -1.0
        new_rot.set_editor_property('yaw', z_rot) #Z
        new_rot.set_editor_property('roll', x_rot) #X      

    if forward_direction[0] == 'Z':
        x_rot = unreal.MathLibrary.deg_atan(new_pos.y / new_pos.z)
        y_rot = unreal.MathLibrary.deg_atan(new_pos.x / new_pos.z) * -1.0
        new_rot.set_editor_property('roll', x_rot) #X    
        new_rot.set_editor_property('pitch', y_rot) #Y 

    # Set the new rotation on the joint
    rtg_controller.set_rotation_offset_for_retarget_pose_bone(target_start_bone, new_rot.quaternion(), unreal.RetargetSourceOrTarget.TARGET)