-- Prankster ghost, first behaviour: drifts around the room along a loop of waypoints, bobbing, facing where it goes.
-- (Ghost AI states, pranks and counterplay come later; this is the patrol baseline.)
local Ghost = {}

--@range 0 6
--@tooltip Drift speed, metres per second
Ghost.speed = 1.2

local WAYPOINTS = {
    { -1.8, -3.8 }, { -4.0, -1.0 }, { -1.5, 2.0 }, { 2.5, 0.5 }, { 3.5, -3.5 },
}

function Ghost:OnCreate()
    self.target = 1
    self.time = 0
    self.base_y = self.entity:Get("Transform").position.y
end

function Ghost:OnUpdate(dt)
    self.time += dt
    local t = self.entity:Get("Transform")
    local p = t.position
    local wp = WAYPOINTS[self.target]
    local dx, dz = wp[1] - p.x, wp[2] - p.z
    local d = math.sqrt(dx * dx + dz * dz)
    if d < 0.2 then
        print(string.format("[Ghost] reached waypoint %d", self.target))
        self.target = self.target % #WAYPOINTS + 1
        return
    end
    local step = math.min(self.speed * dt, d)
    local y = self.base_y + 0.12 * math.sin(self.time * 2.0)
    t.position = vector.create(p.x + dx / d * step, y, p.z + dz / d * step)
    local yaw = math.atan2(-dx, -dz) -- the model's front is -Z
    t.rotation = { 0, math.sin(yaw / 2), 0, math.cos(yaw / 2) }
end

return Ghost
