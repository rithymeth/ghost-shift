-- Ghost Shift contract director (first gameplay slice, single-player authority).
-- Hold Clean (left mouse / right trigger) while facing a contamination within range to scrub it away.
-- Contamination is any entity tagged "Contamination". When none is left the contract is complete.
local Contract = {}

--@tooltip Metres within which the vacuum reaches contamination
Contract.clean_range = 3.0
--@tooltip Seconds of continuous cleaning to remove one piece of contamination
Contract.clean_seconds = 1.5
--@tooltip TEST ONLY: sweep every contamination in order with no input (headless server checks)
Contract.autoclean = false

local function quat_forward(r)
    local x = r.x or r[1]
    local y = r.y or r[2]
    local z = r.z or r[3]
    local w = r.w or r[4]
    return -2 * (x * z + w * y), -(1 - 2 * (x * x + y * y))
end

local function has_tag(entity, wanted)
    if not entity:Has("Tags") then return false end
    for _, tag in ipairs(entity:Get("Tags").names) do
        if tag == wanted then return true end
    end
    return false
end

function Contract:OnCreate()
    self.elapsed = 0
    self.done = false
    self.items = {}
    for _, entity in ipairs(world:EntitiesWith("Tags")) do
        if has_tag(entity, "Contamination") and entity:Has("Transform") then
            local s = entity:Get("Transform").scale
            table.insert(self.items, { entity = entity, hp = 1.0, sx = s.x, sy = s.y, sz = s.z })
        elseif has_tag(entity, "Player") then
            self.player = entity
        end
    end
    self.total = #self.items
    print(string.format("[Contract] started: %d contamination to clean", self.total))
end

function Contract:Target()
    if self.autoclean then return self.items[1] end
    if self.player == nil then return nil end
    local pt = self.player:Get("Transform")
    local p = pt.position
    local fx, fz = quat_forward(pt.rotation)
    local best, best_d = nil, self.clean_range
    for _, item in ipairs(self.items) do
        local q = item.entity:Get("Transform").position
        local dx, dz = q.x - p.x, q.z - p.z
        local d = math.sqrt(dx * dx + dz * dz)
        if d <= best_d and d > 0.01 and (dx * fx + dz * fz) / d > 0.5 then best, best_d = item, d end
    end
    return best
end

function Contract:OnUpdate(dt)
    if self.done then return end
    self.elapsed += dt
    if not (self.autoclean or Input.IsTriggered("Clean")) then return end
    local item = self:Target()
    if item == nil then return end
    item.hp -= dt / self.clean_seconds
    local t = item.entity:Get("Transform")
    local k = math.max(item.hp, 0.05)
    t.scale = vector.create(item.sx * k, item.sy, item.sz * k) -- the puddle shrinks as it is scrubbed
    if item.hp <= 0 then
        for i, other in ipairs(self.items) do
            if other == item then table.remove(self.items, i) break end
        end
        world:Destroy(item.entity)
        print(string.format("[Contract] cleaned one (%d left)", #self.items))
        if #self.items == 0 then
            self.done = true
            print(string.format("[Contract] COMPLETE: %d/%d cleaned in %.1fs", self.total, self.total, self.elapsed))
        end
    end
end

return Contract
