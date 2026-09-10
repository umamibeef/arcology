--  beat.lua -- the moving world, driven.
--
--  The build has a drive and so has this: once a frame the pipeline
--  hands the moving world over, and everything that happens in it is
--  decided here.  How many beats the clock owes is the pipeline's; what
--  a beat DOES is not.
--
--  In each beat: how far every gate swings, and how fast every car goes
--  for the car ahead of it, for the control at the junction it is
--  running towards, and for every level meet on its way.  Then,
--  where the frame wants it drawn, the geometry of what moves, and over
--  it the gates' own moving parts.
--
--  There is no beat in C behind it.  Take this file away and the gates
--  stand still, no car moves and no gate arm is drawn.
--
--  The ORDER the three influences are applied to a car is this file's,
--  and so is the fact that there are three: a fourth -- weather, a speed
--  limit, a driver in a hurry -- is a line here.
--
--  Every step is taken in the world's own precision.  A car's speed is a
--  float where the world keeps it, and a speed carried through three
--  rules in more places than a float can hold lands a hair from where
--  the world puts the car -- which over a minute of beats is a
--  different city entirely.  The numbers each decision is measured
--  against come with the car for the same reason.
local f32 = arc.put.f32

arc.rules.moving = function (b)
    local d = b:info()
    local gate = arc.rules.gate
    local follow, hold = arc.rules.car_follow, arc.rules.car_hold
    local turn = arc.rules.car_turn

    for _ = 1, d.beats do
        --  The trains run on their own rails; the gates and the cars are
        --  what a beat decides.
        --  The trains run on their own rails, and the beat STOPS at each
        --  junction one of them reaches so the arm can be chosen on the
        --  heading it actually arrived with.
        local train_turn = arc.rules.train_turn
        while b:run() do
            local a = b:arms()
            if a and train_turn then b:arm_is(train_turn(a)) end
        end

        --  The gates first: a car reads the gate ahead of it, so they
        --  swing before anything looks at them.
        for i = 0, d.gates - 1 do
            b:gate_is(i, gate and gate(b:gate(i)))
        end

        --  Then the cars, each read and moved before the next is read: a
        --  car looks at the one ahead, which has already moved.
        for i = 0, b:cars() - 1 do
            local c = b:car(i)
            if c then
                local v = c.speed
                --  the car ahead in the same lane
                if c.gap and follow then
                    v = f32(follow{gap = c.gap, speed = v, stop = c.stop, free = c.free})
                end
                --  the control at the junction it is running towards
                if c.ahead and hold then
                    --  a signal says for itself whether it is red; a stop
                    --  sign's hold is counted by the world
                    local held = c.held
                    if c.signal then
                        local sig = arc.rules.signal
                        held = sig and sig(c.signal) or false
                    end
                    v = f32(hold{ahead = c.ahead, held = held, line = c.line,
                                 speed = v, step = c.step})
                end
                --  and the arm it takes at the junction it is a beat
                --  away from, chosen while it can still be told
                if c.arms and turn then
                    b:car_turn_is(turn{arms = c.arms, draw = c.draw})
                end
                --  and every meet on its way
                if hold then
                    for _, ahead in ipairs(c.meets) do
                        v = f32(hold{ahead = ahead, held = true, line = c.creep,
                                     speed = v, step = c.step})
                    end
                end
                b:car_is(i, v)
            end
        end
    end

    --  And the world DRAWN, where the frame asked for it: what moves as
    --  geometry, and over it each gate's flashers and the striped arm it
    --  has swung to, which are arc.rules.gate_arm's.
    if d.draw then
        --  The thread signals' aspects, settled before the movers are laid:
        --  the world says how near the nearest car is, the rule what the
        --  signal shows.
        local thread_signal = arc.rules.thread_signal
        for i = 0, b:signals() - 1 do
            local sg = b:signal(i)
            if sg then b:signal_is(i, thread_signal and thread_signal(sg)) end
        end
        b:build()
        local arm = arc.rules.gate_arm
        for i = 0, b:gates_drawn() - 1 do
            local at = b:gate_prop(i)
            if at and arm then arm(at) end
            b:gate_drawn()
        end
    end
    return true
end
