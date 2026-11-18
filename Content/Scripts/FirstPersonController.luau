-- First person controller: WASD or the left stick to move, the mouse or the
-- right stick to look, Space or the A button to jump. Put it on the entity that
-- has the Camera; the height it starts at is the floor.
local Controller = {}

--@range 0 30
--@tooltip Walking speed, metres per second
Controller.speed = 5
--@range 0 1
--@tooltip Degrees of turn per pixel of mouse movement
Controller.look_speed = 0.12
--@range 0 20
Controller.jump_speed = 5
--@range -60 0
Controller.gravity = -15

function Controller:OnCreate()
    self.yaw = 0     -- radians, turning left is positive
    self.pitch = 0   -- radians, looking up is positive
    self.vy = 0
    self.floor = self.entity:Get("Transform").position.y
end

function Controller:OnUpdate(dt)
    local t = self.entity:Get("Transform")

    local look = Input.GetAxis2D("Look")
    self.yaw = self.yaw - math.rad(look.x * self.look_speed)
    self.pitch = math.clamp(self.pitch - math.rad(look.y * self.look_speed), -1.4, 1.4)

    local move = Input.GetAxis2D("Move")
    if vector.magnitude(move) > 1 then move = vector.normalize(move) end
    local sy, cy = math.sin(self.yaw), math.cos(self.yaw)
    -- Forward is (-sin, 0, -cos) and right is (cos, 0, -sin): the camera looks down -Z.
    local vx = (-sy * move.y + cy * move.x) * self.speed
    local vz = (-cy * move.y - sy * move.x) * self.speed

    if self.vy == 0 and Input.IsTriggered("Jump") then self.vy = self.jump_speed end
    self.vy += self.gravity * dt
    local p = t.position
    local y = p.y + self.vy * dt
    if y <= self.floor then y = self.floor; self.vy = 0 end

    t.position = vector.create(p.x + vx * dt, y, p.z + vz * dt)
    -- Yaw about Y, then pitch about X, as a quaternion (x, y, z, w).
    local hy, hp = self.yaw / 2, self.pitch / 2
    t.rotation = {
        math.cos(hy) * math.sin(hp), math.sin(hy) * math.cos(hp),
        -math.sin(hy) * math.sin(hp), math.cos(hy) * math.cos(hp),
    }
end

return Controller
