from __future__ import print_function
import msgpackrpc #install as admin: pip install msgpack-rpc-python
import numpy as np #pip install numpy
import math

def _coerce_msgpack_key(key):
    return key.decode('utf-8') if isinstance(key, bytes) else key

def _decode_msgpack_value(template, value):
    decoder = getattr(getattr(template, "__class__", None), "from_msgpack", None)
    if decoder is not None and isinstance(value, (dict, list, tuple)):
        return decoder(value)
    return value

class MsgpackMixin:
    def __repr__(self):
        from pprint import pformat
        return "<" + type(self).__name__ + "> " + pformat(vars(self), indent=4, width=1)

    @classmethod
    def _msgpack_fields(cls):
        fields = getattr(cls, "_airsim_msgpack_fields_", ())
        return tuple(fields) if fields else ()

    def to_msgpack(self, *args, **kwargs):
        fields = self._msgpack_fields()
        if fields:
            return [getattr(self, key) for key in fields]
        return self.__dict__

    @classmethod
    def from_msgpack(cls, encoded):
        obj = cls()
        fields = obj._msgpack_fields()

        if isinstance(encoded, dict):
            items = [(_coerce_msgpack_key(k), v) for k, v in encoded.items()]
        elif isinstance(encoded, (list, tuple)):
            if not fields:
                raise TypeError("Array msgpack requires explicit field order for %s" % cls.__name__)
            items = list(zip(fields, encoded))
        else:
            raise TypeError("Unsupported msgpack payload for %s: %s" % (cls.__name__, type(encoded).__name__))

        decoded = {}
        for key, value in items:
            decoded[key] = _decode_msgpack_value(getattr(obj, key, None), value)

        obj.__dict__ = decoded
        return obj

class _ImageType(type):
    @property
    def Scene(cls):
        return 0
    def DepthPlanar(cls):
        return 1
    def DepthPerspective(cls):
        return 2
    def DepthVis(cls):
        return 3
    def DisparityNormalized(cls):
        return 4
    def Segmentation(cls):
        return 5
    def SurfaceNormals(cls):
        return 6
    def Infrared(cls):
        return 7
    def OpticalFlow(cls):
        return 8
    def OpticalFlowVis(cls):
        return 9

    def __getattr__(self, key):
        if key == 'DepthPlanner':
            print('\033[31m'+"DepthPlanner has been (correctly) renamed to DepthPlanar. Please use ImageType.DepthPlanar instead."+'\033[0m')
            raise AttributeError

class ImageType(metaclass=_ImageType):
    Scene = 0
    DepthPlanar = 1
    DepthPerspective = 2
    DepthVis = 3
    DisparityNormalized = 4
    Segmentation = 5
    SurfaceNormals = 6
    Infrared = 7
    OpticalFlow = 8
    OpticalFlowVis = 9

class DrivetrainType:
    MaxDegreeOfFreedom = 0
    ForwardOnly = 1

class LandedState:
    Landed = 0
    Flying = 1

class WeatherParameter:
    Rain = 0
    Roadwetness = 1
    Snow = 2
    RoadSnow = 3
    MapleLeaf = 4
    RoadLeaf = 5
    Dust = 6
    Fog = 7
    Enabled = 8

class Vector2r(MsgpackMixin):
    _airsim_msgpack_fields_ = ('x_val', 'y_val')
    x_val = 0.0
    y_val = 0.0

    def __init__(self, x_val = 0.0, y_val = 0.0):
        self.x_val = x_val
        self.y_val = y_val

class Vector3r(MsgpackMixin):
    _airsim_msgpack_fields_ = ('x_val', 'y_val', 'z_val')
    x_val = 0.0
    y_val = 0.0
    z_val = 0.0

    def __init__(self, x_val = 0.0, y_val = 0.0, z_val = 0.0):
        self.x_val = x_val
        self.y_val = y_val
        self.z_val = z_val

    @staticmethod
    def nanVector3r():
        return Vector3r(np.nan, np.nan, np.nan)

    def containsNan(self):
        return (math.isnan(self.x_val) or math.isnan(self.y_val) or math.isnan(self.z_val))

    def __add__(self, other):
        return Vector3r(self.x_val + other.x_val, self.y_val + other.y_val, self.z_val + other.z_val)

    def __sub__(self, other):
        return Vector3r(self.x_val - other.x_val, self.y_val - other.y_val, self.z_val - other.z_val)

    def __truediv__(self, other):
        if type(other) in [int, float] + np.sctypes['int'] + np.sctypes['uint'] + np.sctypes['float']:
            return Vector3r( self.x_val / other, self.y_val / other, self.z_val / other)
        else:
            raise TypeError('unsupported operand type(s) for /: %s and %s' % ( str(type(self)), str(type(other))) )

    def __mul__(self, other):
        if type(other) in [int, float] + np.sctypes['int'] + np.sctypes['uint'] + np.sctypes['float']:
            return Vector3r(self.x_val*other, self.y_val*other, self.z_val*other)
        else:
            raise TypeError('unsupported operand type(s) for *: %s and %s' % ( str(type(self)), str(type(other))) )

    def dot(self, other):
        if type(self) == type(other):
            return self.x_val*other.x_val + self.y_val*other.y_val + self.z_val*other.z_val
        else:
            raise TypeError('unsupported operand type(s) for \'dot\': %s and %s' % ( str(type(self)), str(type(other))) )

    def cross(self, other):
        if type(self) == type(other):
            cross_product = np.cross(self.to_numpy_array(), other.to_numpy_array())
            return Vector3r(cross_product[0], cross_product[1], cross_product[2])
        else:
            raise TypeError('unsupported operand type(s) for \'cross\': %s and %s' % ( str(type(self)), str(type(other))) )

    def get_length(self):
        return ( self.x_val**2 + self.y_val**2 + self.z_val**2 )**0.5

    def distance_to(self, other):
        return ( (self.x_val-other.x_val)**2 + (self.y_val-other.y_val)**2 + (self.z_val-other.z_val)**2 )**0.5

    def to_Quaternionr(self):
        return Quaternionr(self.x_val, self.y_val, self.z_val, 0)

    def to_numpy_array(self):
        return np.array([self.x_val, self.y_val, self.z_val], dtype=np.float32)

    def __iter__(self):
        return iter((self.x_val, self.y_val, self.z_val))

class Quaternionr(MsgpackMixin):
    _airsim_msgpack_fields_ = ('w_val', 'x_val', 'y_val', 'z_val')
    w_val = 0.0
    x_val = 0.0
    y_val = 0.0
    z_val = 0.0

    def __init__(self, x_val = 0.0, y_val = 0.0, z_val = 0.0, w_val = 1.0):
        self.x_val = x_val
        self.y_val = y_val
        self.z_val = z_val
        self.w_val = w_val

    @staticmethod
    def nanQuaternionr():
        return Quaternionr(np.nan, np.nan, np.nan, np.nan)

    def containsNan(self):
        return (math.isnan(self.w_val) or math.isnan(self.x_val) or math.isnan(self.y_val) or math.isnan(self.z_val))

    def __add__(self, other):
        if type(self) == type(other):
            return Quaternionr( self.x_val+other.x_val, self.y_val+other.y_val, self.z_val+other.z_val, self.w_val+other.w_val )
        else:
            raise TypeError('unsupported operand type(s) for +: %s and %s' % ( str(type(self)), str(type(other))) )

    def __mul__(self, other):
        if type(self) == type(other):
            t, x, y, z = self.w_val, self.x_val, self.y_val, self.z_val
            a, b, c, d = other.w_val, other.x_val, other.y_val, other.z_val
            return Quaternionr( w_val = a*t - b*x - c*y - d*z,
                                x_val = b*t + a*x + d*y - c*z,
                                y_val = c*t + a*y + b*z - d*x,
                                z_val = d*t + z*a + c*x - b*y)
        else:
            raise TypeError('unsupported operand type(s) for *: %s and %s' % ( str(type(self)), str(type(other))) )

    def __truediv__(self, other):
        if type(other) == type(self):
            return self * other.inverse()
        elif type(other) in [int, float] + np.sctypes['int'] + np.sctypes['uint'] + np.sctypes['float']:
            return Quaternionr( self.x_val / other, self.y_val / other, self.z_val / other, self.w_val / other)
        else:
            raise TypeError('unsupported operand type(s) for /: %s and %s' % ( str(type(self)), str(type(other))) )

    def dot(self, other):
        if type(self) == type(other):
            return self.x_val*other.x_val + self.y_val*other.y_val + self.z_val*other.z_val + self.w_val*other.w_val
        else:
            raise TypeError('unsupported operand type(s) for \'dot\': %s and %s' % ( str(type(self)), str(type(other))) )

    def cross(self, other):
        if type(self) == type(other):
            return (self * other - other * self) / 2
        else:
            raise TypeError('unsupported operand type(s) for \'cross\': %s and %s' % ( str(type(self)), str(type(other))) )

    def outer_product(self, other):
        if type(self) == type(other):
            return ( self.inverse()*other - other.inverse()*self ) / 2
        else:
            raise TypeError('unsupported operand type(s) for \'outer_product\': %s and %s' % ( str(type(self)), str(type(other))) )

    def rotate(self, other):
        if type(self) == type(other):
            if other.get_length() == 1:
                return other * self * other.inverse()
            else:
                raise ValueError('length of the other Quaternionr must be 1')
        else:
            raise TypeError('unsupported operand type(s) for \'rotate\': %s and %s' % ( str(type(self)), str(type(other))) )

    def conjugate(self):
        return Quaternionr(-self.x_val, -self.y_val, -self.z_val, self.w_val)

    def star(self):
        return self.conjugate()

    def inverse(self):
        return self.star() / self.dot(self)

    def sgn(self):
        return self/self.get_length()

    def get_length(self):
        return ( self.x_val**2 + self.y_val**2 + self.z_val**2 + self.w_val**2 )**0.5

    def to_numpy_array(self):
        return np.array([self.x_val, self.y_val, self.z_val, self.w_val], dtype=np.float32)

    def __iter__(self):
        return iter((self.x_val, self.y_val, self.z_val, self.w_val))

class Pose(MsgpackMixin):
    _airsim_msgpack_fields_ = ('position', 'orientation')
    position = Vector3r()
    orientation = Quaternionr()

    def __init__(self, position_val = None, orientation_val = None):
        position_val = position_val if position_val is not None else Vector3r()
        orientation_val = orientation_val if orientation_val is not None else Quaternionr()
        self.position = position_val
        self.orientation = orientation_val

    @staticmethod
    def nanPose():
        return Pose(Vector3r.nanVector3r(), Quaternionr.nanQuaternionr())

    def containsNan(self):
        return (self.position.containsNan() or self.orientation.containsNan())

    def __iter__(self):
        return iter((self.position, self.orientation))

class CollisionInfo(MsgpackMixin):
    _airsim_msgpack_fields_ = (
        'has_collided', 'penetration_depth', 'time_stamp', 'normal',
        'impact_point', 'position', 'object_name', 'object_id'
    )
    has_collided = False
    normal = Vector3r()
    impact_point = Vector3r()
    position = Vector3r()
    penetration_depth = 0.0
    time_stamp = 0.0
    object_name = ""
    object_id = -1

class GeoPoint(MsgpackMixin):
    _airsim_msgpack_fields_ = ('latitude', 'longitude', 'altitude')
    latitude = 0.0
    longitude = 0.0
    altitude = 0.0

class YawMode(MsgpackMixin):
    _airsim_msgpack_fields_ = ('is_rate', 'yaw_or_rate')
    is_rate = True
    yaw_or_rate = 0.0
    def __init__(self, is_rate = True, yaw_or_rate = 0.0):
        self.is_rate = is_rate
        self.yaw_or_rate = yaw_or_rate

class RCData(MsgpackMixin):
    _airsim_msgpack_fields_ = (
        'timestamp', 'pitch', 'roll', 'throttle', 'yaw',
        'left_z', 'right_z', 'switches', 'vendor_id',
        'is_initialized', 'is_valid'
    )
    timestamp = 0
    pitch, roll, throttle, yaw = (0.0,)*4 #init 4 variable to 0.0
    left_z = 0.0
    right_z = 0.0
    switches = 0
    vendor_id = ""
    switch1, switch2, switch3, switch4 = (0,)*4
    switch5, switch6, switch7, switch8 = (0,)*4
    is_initialized = False
    is_valid = False
    def __init__(self, timestamp = 0, pitch = 0.0, roll = 0.0, throttle = 0.0, yaw = 0.0, switch1 = 0,
                 switch2 = 0, switch3 = 0, switch4 = 0, switch5 = 0, switch6 = 0, switch7 = 0, switch8 = 0, is_initialized = False, is_valid = False):
        self.timestamp = timestamp
        self.pitch = pitch
        self.roll = roll
        self.throttle = throttle
        self.yaw = yaw
        self.switch1 = switch1
        self.switch2 = switch2
        self.switch3 = switch3
        self.switch4 = switch4
        self.switch5 = switch5
        self.switch6 = switch6
        self.switch7 = switch7
        self.switch8 = switch8
        self.left_z = 0.0
        self.right_z = 0.0
        self.switches = (
            ((1 if switch1 else 0) << 0) |
            ((1 if switch2 else 0) << 1) |
            ((1 if switch3 else 0) << 2) |
            ((1 if switch4 else 0) << 3) |
            ((1 if switch5 else 0) << 4) |
            ((1 if switch6 else 0) << 5) |
            ((1 if switch7 else 0) << 6) |
            ((1 if switch8 else 0) << 7)
        )
        self.vendor_id = ""
        self.is_initialized = is_initialized
        self.is_valid = is_valid

    def to_msgpack(self, *args, **kwargs):
        switches = getattr(self, 'switches', 0)
        if not switches:
            switches = (
                ((1 if getattr(self, 'switch1', 0) else 0) << 0) |
                ((1 if getattr(self, 'switch2', 0) else 0) << 1) |
                ((1 if getattr(self, 'switch3', 0) else 0) << 2) |
                ((1 if getattr(self, 'switch4', 0) else 0) << 3) |
                ((1 if getattr(self, 'switch5', 0) else 0) << 4) |
                ((1 if getattr(self, 'switch6', 0) else 0) << 5) |
                ((1 if getattr(self, 'switch7', 0) else 0) << 6) |
                ((1 if getattr(self, 'switch8', 0) else 0) << 7)
            )

        return [
            self.timestamp,
            self.pitch,
            self.roll,
            self.throttle,
            self.yaw,
            getattr(self, 'left_z', 0.0),
            getattr(self, 'right_z', 0.0),
            switches,
            getattr(self, 'vendor_id', ""),
            self.is_initialized,
            self.is_valid,
        ]

    @classmethod
    def from_msgpack(cls, encoded):
        obj = super(RCData, cls).from_msgpack(encoded)
        switches = int(getattr(obj, 'switches', 0))
        obj.switch1 = (switches >> 0) & 1
        obj.switch2 = (switches >> 1) & 1
        obj.switch3 = (switches >> 2) & 1
        obj.switch4 = (switches >> 3) & 1
        obj.switch5 = (switches >> 4) & 1
        obj.switch6 = (switches >> 5) & 1
        obj.switch7 = (switches >> 6) & 1
        obj.switch8 = (switches >> 7) & 1
        return obj

class ImageRequest(MsgpackMixin):
    _airsim_msgpack_fields_ = ('camera_name', 'image_type', 'pixels_as_float', 'compress')
    camera_name = '0'
    image_type = ImageType.Scene
    pixels_as_float = False
    compress = False

    def __init__(self, camera_name, image_type, pixels_as_float = False, compress = True):
        # todo: in future remove str(), it's only for compatibility to pre v1.2
        self.camera_name = str(camera_name)
        self.image_type = image_type
        self.pixels_as_float = pixels_as_float
        self.compress = compress


class ImageResponse(MsgpackMixin):
    _airsim_msgpack_fields_ = (
        'image_data_uint8', 'image_data_float', 'camera_position', 'camera_name',
        'camera_orientation', 'time_stamp', 'message', 'pixels_as_float',
        'compress', 'width', 'height', 'image_type'
    )
    image_data_uint8 = np.uint8(0)
    image_data_float = 0.0
    camera_position = Vector3r()
    camera_name = ''
    camera_orientation = Quaternionr()
    time_stamp = np.uint64(0)
    message = ''
    pixels_as_float = 0.0
    compress = True
    width = 0
    height = 0
    image_type = ImageType.Scene

class CarControls(MsgpackMixin):
    throttle = 0.0
    steering = 0.0
    brake = 0.0
    handbrake = False
    is_manual_gear = False
    manual_gear = 0
    gear_immediate = True

    def __init__(self, throttle = 0, steering = 0, brake = 0,
        handbrake = False, is_manual_gear = False, manual_gear = 0, gear_immediate = True):
        self.throttle = throttle
        self.steering = steering
        self.brake = brake
        self.handbrake = handbrake
        self.is_manual_gear = is_manual_gear
        self.manual_gear = manual_gear
        self.gear_immediate = gear_immediate


    def set_throttle(self, throttle_val, forward):
        if (forward):
            self.is_manual_gear = False
            self.manual_gear = 0
            self.throttle = abs(throttle_val)
        else:
            self.is_manual_gear = False
            self.manual_gear = -1
            self.throttle = - abs(throttle_val)

class KinematicsState(MsgpackMixin):
    _airsim_msgpack_fields_ = (
        'position', 'orientation', 'linear_velocity',
        'angular_velocity', 'linear_acceleration', 'angular_acceleration'
    )
    position = Vector3r()
    orientation = Quaternionr()
    linear_velocity = Vector3r()
    angular_velocity = Vector3r()
    linear_acceleration = Vector3r()
    angular_acceleration = Vector3r()

class EnvironmentState(MsgpackMixin):
    _airsim_msgpack_fields_ = (
        'position', 'geo_point', 'gravity',
        'air_pressure', 'temperature', 'air_density'
    )
    position = Vector3r()
    geo_point = GeoPoint()
    gravity = Vector3r()
    air_pressure = 0.0
    temperature = 0.0
    air_density = 0.0

class CarState(MsgpackMixin):
    speed = 0.0
    gear = 0
    rpm = 0.0
    maxrpm = 0.0
    handbrake = False
    collision = CollisionInfo()
    kinematics_estimated = KinematicsState()
    timestamp = np.uint64(0)

class MultirotorState(MsgpackMixin):
    _airsim_msgpack_fields_ = (
        'collision', 'kinematics_estimated', 'gps_location',
        'timestamp', 'landed_state', 'rc_data',
        'ready', 'ready_message', 'can_arm'
    )
    collision = CollisionInfo()
    kinematics_estimated = KinematicsState()
    gps_location = GeoPoint()
    timestamp = np.uint64(0)
    landed_state = LandedState.Landed
    rc_data = RCData()
    ready = False
    ready_message = ""
    can_arm = False

class RotorStates(MsgpackMixin):
    _airsim_msgpack_fields_ = ('rotors', 'timestamp')
    rotors = []
    timestamp = np.uint64(0)

class ProjectionMatrix(MsgpackMixin):
    _airsim_msgpack_fields_ = ('matrix',)
    matrix = []

class CameraInfo(MsgpackMixin):
    _airsim_msgpack_fields_ = ('pose', 'fov', 'proj_mat')
    pose = Pose()
    fov = -1
    proj_mat = ProjectionMatrix()

class LidarData(MsgpackMixin):
    _airsim_msgpack_fields_ = ('time_stamp', 'point_cloud', 'pose', 'segmentation')
    point_cloud = 0.0
    time_stamp = np.uint64(0)
    pose = Pose()
    segmentation = 0

class ImuData(MsgpackMixin):
    _airsim_msgpack_fields_ = ('time_stamp', 'orientation', 'angular_velocity', 'linear_acceleration')
    time_stamp = np.uint64(0)
    orientation = Quaternionr()
    angular_velocity = Vector3r()
    linear_acceleration = Vector3r()

class BarometerData(MsgpackMixin):
    _airsim_msgpack_fields_ = ('time_stamp', 'altitude', 'pressure', 'qnh')
    time_stamp = np.uint64(0)
    altitude = Quaternionr()
    pressure = Vector3r()
    qnh = Vector3r()

class MagnetometerData(MsgpackMixin):
    _airsim_msgpack_fields_ = ('time_stamp', 'magnetic_field_body', 'magnetic_field_covariance')
    time_stamp = np.uint64(0)
    magnetic_field_body = Vector3r()
    magnetic_field_covariance = 0.0

class GnssFixType(MsgpackMixin):
    GNSS_FIX_NO_FIX = 0
    GNSS_FIX_TIME_ONLY = 1
    GNSS_FIX_2D_FIX = 2
    GNSS_FIX_3D_FIX = 3

class GnssReport(MsgpackMixin):
    _airsim_msgpack_fields_ = ('geo_point', 'eph', 'epv', 'velocity', 'fix_type', 'time_utc')
    geo_point = GeoPoint()
    eph = 0.0
    epv = 0.0
    velocity = Vector3r()
    fix_type = GnssFixType()
    time_utc = np.uint64(0)

class GpsData(MsgpackMixin):
    _airsim_msgpack_fields_ = ('time_stamp', 'gnss', 'is_valid')
    time_stamp = np.uint64(0)
    gnss = GnssReport()
    is_valid = False

class DistanceSensorData(MsgpackMixin):
    _airsim_msgpack_fields_ = ('time_stamp', 'distance', 'min_distance', 'max_distance', 'relative_pose')
    time_stamp = np.uint64(0)
    distance = 0.0
    min_distance = 0.0
    max_distance = 0.0
    relative_pose = Pose()

class Box2D(MsgpackMixin):
    _airsim_msgpack_fields_ = ('min', 'max')
    min = Vector2r()
    max = Vector2r()

class Box3D(MsgpackMixin):
    _airsim_msgpack_fields_ = ('min', 'max')
    min = Vector3r()
    max = Vector3r()

class DetectionInfo(MsgpackMixin):
    _airsim_msgpack_fields_ = ('name', 'geo_point', 'box2D', 'box3D', 'relative_pose')
    name = ''
    geo_point = GeoPoint()
    box2D = Box2D()
    box3D = Box3D()
    relative_pose = Pose()
    
class PIDGains():
    """
    Struct to store values of PID gains. Used to transmit controller gain values while instantiating
    AngleLevel/AngleRate/Velocity/PositionControllerGains objects.

    Attributes:
        kP (float): Proportional gain
        kI (float): Integrator gain
        kD (float): Derivative gain
    """
    def __init__(self, kp, ki, kd):
        self.kp = kp
        self.ki = ki
        self.kd = kd

    def to_list(self):
        return [self.kp, self.ki, self.kd]

class AngleRateControllerGains():
    """
    Struct to contain controller gains used by angle level PID controller

    Attributes:
        roll_gains (PIDGains): kP, kI, kD for roll axis
        pitch_gains (PIDGains): kP, kI, kD for pitch axis
        yaw_gains (PIDGains): kP, kI, kD for yaw axis
    """
    def __init__(self, roll_gains = PIDGains(0.25, 0, 0),
                       pitch_gains = PIDGains(0.25, 0, 0),
                       yaw_gains = PIDGains(0.25, 0, 0)):
        self.roll_gains = roll_gains
        self.pitch_gains = pitch_gains
        self.yaw_gains = yaw_gains

    def to_lists(self):
        return [self.roll_gains.kp, self.pitch_gains.kp, self.yaw_gains.kp], [self.roll_gains.ki, self.pitch_gains.ki, self.yaw_gains.ki], [self.roll_gains.kd, self.pitch_gains.kd, self.yaw_gains.kd]

class AngleLevelControllerGains():
    """
    Struct to contain controller gains used by angle rate PID controller

    Attributes:
        roll_gains (PIDGains): kP, kI, kD for roll axis
        pitch_gains (PIDGains): kP, kI, kD for pitch axis
        yaw_gains (PIDGains): kP, kI, kD for yaw axis
    """
    def __init__(self, roll_gains = PIDGains(2.5, 0, 0),
                       pitch_gains = PIDGains(2.5, 0, 0),
                       yaw_gains = PIDGains(2.5, 0, 0)):
        self.roll_gains = roll_gains
        self.pitch_gains = pitch_gains
        self.yaw_gains = yaw_gains

    def to_lists(self):
        return [self.roll_gains.kp, self.pitch_gains.kp, self.yaw_gains.kp], [self.roll_gains.ki, self.pitch_gains.ki, self.yaw_gains.ki], [self.roll_gains.kd, self.pitch_gains.kd, self.yaw_gains.kd]

class VelocityControllerGains():
    """
    Struct to contain controller gains used by velocity PID controller

    Attributes:
        x_gains (PIDGains): kP, kI, kD for X axis
        y_gains (PIDGains): kP, kI, kD for Y axis
        z_gains (PIDGains): kP, kI, kD for Z axis
    """
    def __init__(self, x_gains = PIDGains(0.2, 0, 0),
                       y_gains = PIDGains(0.2, 0, 0),
                       z_gains = PIDGains(2.0, 2.0, 0)):
        self.x_gains = x_gains
        self.y_gains = y_gains
        self.z_gains = z_gains

    def to_lists(self):
        return [self.x_gains.kp, self.y_gains.kp, self.z_gains.kp], [self.x_gains.ki, self.y_gains.ki, self.z_gains.ki], [self.x_gains.kd, self.y_gains.kd, self.z_gains.kd]

class PositionControllerGains():
    """
    Struct to contain controller gains used by position PID controller

    Attributes:
        x_gains (PIDGains): kP, kI, kD for X axis
        y_gains (PIDGains): kP, kI, kD for Y axis
        z_gains (PIDGains): kP, kI, kD for Z axis
    """
    def __init__(self, x_gains = PIDGains(0.25, 0, 0),
                       y_gains = PIDGains(0.25, 0, 0),
                       z_gains = PIDGains(0.25, 0, 0)):
        self.x_gains = x_gains
        self.y_gains = y_gains
        self.z_gains = z_gains

    def to_lists(self):
        return [self.x_gains.kp, self.y_gains.kp, self.z_gains.kp], [self.x_gains.ki, self.y_gains.ki, self.z_gains.ki], [self.x_gains.kd, self.y_gains.kd, self.z_gains.kd]

class MeshPositionVertexBuffersResponse(MsgpackMixin):
    _airsim_msgpack_fields_ = ('position', 'orientation', 'vertices', 'indices', 'name')
    position = Vector3r()
    orientation = Quaternionr()
    vertices = 0.0
    indices = 0.0
    name = ''
