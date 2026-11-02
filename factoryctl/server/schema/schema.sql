-- The whole database of factoryctl. The data is not critical: a schema that
-- no longer fits is dropped and made again, there are no migrations.
-- Bump the version below on every change; the server refuses to start on a
-- database of another version and says how to drop it.

CREATE SCHEMA factoryctl;

CREATE TABLE factoryctl.schema_version (
    version integer NOT NULL
);
INSERT INTO factoryctl.schema_version VALUES (10);

-- Anything the ME network holds: an item, or a fluid of getFluidsInNetwork.
-- Key as in domain::ItemKey: `variant` is the fluid's internal name for fluid
-- drops (ae2fc:fluid_drop, told apart by NBT only) and '' for everything else.
CREATE TYPE factoryctl.goods_kind AS ENUM ('item', 'fluid');

CREATE TABLE factoryctl.goods (
    id        bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    kind      factoryctl.goods_kind NOT NULL,
    name      text NOT NULL,             -- 'gregtech:gt.metaitem.01', 'molten.platinum'
    damage    integer NOT NULL DEFAULT 0,
    variant   text NOT NULL DEFAULT '',  -- 'molten.platinum' for a drop
    label     text NOT NULL,             -- as the game shows it, latest seen
    UNIQUE (kind, name, damage, variant)
);

-- A scan of the ME network, made of batches from the agent. Only a finished
-- scan says anything about what is absent.
CREATE TABLE factoryctl.scans (
    id          bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    started_at  timestamptz NOT NULL DEFAULT now(),
    finished_at timestamptz
);

-- Current stock. A row whose scan_id is older than the last finished scan
-- was not seen in it: its stock is 0 (rows are kept for the labels).
-- probed_at: the agent measured it then (targets, every poll); a probe
-- moves scan_id up to the last finished scan.
-- craftable: AE has a pattern for it (isCraftable). A finished scan clears it
-- for what it did not see (craftable goods are always scanned); the agent's
-- probes and orders tell it between scans.
CREATE TABLE factoryctl.stock (
    goods_id  bigint PRIMARY KEY REFERENCES factoryctl.goods ON DELETE CASCADE,
    amount    bigint NOT NULL,
    scan_id   bigint NOT NULL REFERENCES factoryctl.scans,
    probed_at timestamptz,
    craftable boolean NOT NULL
);

-- What to keep stocked. threshold 0 = order whenever nothing is on order;
-- fill = order what is missing, in whole batches, instead of one batch.
CREATE TYPE factoryctl.priority AS ENUM ('high', 'low');

-- Groups of targets, for people: they change nothing in what is ordered. A
-- deleted group leaves its targets, ungrouped.
CREATE TABLE factoryctl.target_groups (
    id   bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    name text NOT NULL UNIQUE CHECK (name <> '')
);

CREATE TABLE factoryctl.targets (
    goods_id   bigint PRIMARY KEY REFERENCES factoryctl.goods ON DELETE CASCADE,
    threshold  bigint NOT NULL CHECK (threshold >= 0),
    batch      bigint NOT NULL CHECK (batch > 0),
    priority   factoryctl.priority NOT NULL DEFAULT 'high',
    enabled    boolean NOT NULL DEFAULT true,
    note       text NOT NULL DEFAULT '',
    fill       boolean NOT NULL DEFAULT false,
    group_id   bigint REFERENCES factoryctl.target_groups ON DELETE SET NULL,
    updated_at timestamptz NOT NULL DEFAULT now()
);

-- Stock over time, for targets only: written when a scan finishes.
CREATE TABLE factoryctl.stock_history (
    goods_id bigint NOT NULL REFERENCES factoryctl.goods ON DELETE CASCADE,
    at       timestamptz NOT NULL,
    amount   bigint NOT NULL,
    PRIMARY KEY (goods_id, at)
);

-- Orders, as domain::OrderState.
CREATE TYPE factoryctl.order_status AS ENUM (
    'queued', 'sent', 'computing', 'accepted', 'crafting',
    'done', 'failed', 'canceled', 'lost'
);
CREATE TYPE factoryctl.order_source AS ENUM ('auto', 'manual');

CREATE TABLE factoryctl.orders (
    id               bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    goods_id         bigint NOT NULL REFERENCES factoryctl.goods,
    amount           bigint NOT NULL CHECK (amount > 0),
    status           factoryctl.order_status NOT NULL DEFAULT 'queued',
    source           factoryctl.order_source NOT NULL,
    requested_by     text NOT NULL DEFAULT '',  -- a name typed in the client
    created_at       timestamptz NOT NULL DEFAULT now(),
    changed_at       timestamptz NOT NULL DEFAULT now(),
    message          text NOT NULL DEFAULT '',
    cpu              text NOT NULL DEFAULT '',
    remaining        bigint NOT NULL DEFAULT 0,
    progress_at      timestamptz,
    cancel_requested boolean NOT NULL DEFAULT false,
    alarm_raised     boolean NOT NULL DEFAULT false
);

-- At most one open order per goods: the CPU running an order is found by its
-- item, which needs this. Manual orders obey it too.
CREATE UNIQUE INDEX orders_one_open ON factoryctl.orders (goods_id)
    WHERE status IN ('queued', 'sent', 'computing', 'accepted', 'crafting');
CREATE INDEX orders_by_time ON factoryctl.orders (created_at DESC);

-- CPUs as of the last poll; replaced on every poll.
CREATE TABLE factoryctl.cpus (
    name      text PRIMARY KEY,
    busy      boolean NOT NULL,
    goods_id  bigint REFERENCES factoryctl.goods,
    remaining bigint NOT NULL DEFAULT 0,
    seen_at   timestamptz NOT NULL
);

-- What happened, for people: the agent's log lines, order status changes,
-- alarms, scans, targets. Not the server's whole log (that stays on stderr).
-- Written in the transaction of what it tells; kept for events-keep.
CREATE TYPE factoryctl.event_level AS ENUM ('debug', 'info', 'warning', 'error');

CREATE TABLE factoryctl.events (
    id           bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    at           timestamptz NOT NULL,
    level        factoryctl.event_level NOT NULL,
    source       text NOT NULL,  -- agent | server | order | scan | target | pumps | qft | mining
    text         text NOT NULL,
    order_id     bigint REFERENCES factoryctl.orders ON DELETE SET NULL,
    goods_id     bigint REFERENCES factoryctl.goods ON DELETE SET NULL,
    agent_uptime double precision  -- the agent's computer.uptime(), its lines only
);
CREATE INDEX events_by_time ON factoryctl.events (at);
CREATE INDEX events_of_order ON factoryctl.events (order_id) WHERE order_id IS NOT NULL;

-- The pumping agent (factoryctl/pumps): it decides; the server keeps its
-- settings and what it reports. Fluids by internal name (goods.variant).
-- One row: the settings, `version` up with every change (the agent takes
-- newer ones). No row: the agent offers its own.
CREATE TABLE factoryctl.pump_settings (
    one        boolean PRIMARY KEY DEFAULT true CHECK (one),
    version    bigint NOT NULL CHECK (version > 0),
    batch      integer NOT NULL CHECK (batch BETWEEN 1 AND 128),
    threshold  double precision NOT NULL CHECK (threshold > 0 AND threshold <= 1),
    updated_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE factoryctl.pump_targets (
    fluid    text PRIMARY KEY,
    target   bigint NOT NULL CHECK (target >= 0),
    priority double precision NOT NULL CHECK (priority > 0),
    enabled  boolean NOT NULL DEFAULT true
);

-- What the modules can pump, as the agent says (its fp/recipes.lua).
CREATE TABLE factoryctl.pump_recipes (
    fluid  text PRIMARY KEY,
    planet integer NOT NULL,
    gas    integer NOT NULL,
    label  text NOT NULL,
    rate   bigint NOT NULL
);

-- The modules as of the last poll; replaced on every poll. Slots of the
-- cycle a module runs, as parallel arrays.
CREATE TABLE factoryctl.pump_modules (
    address      text PRIMARY KEY,
    tier         integer NOT NULL,
    state        text NOT NULL,
    fluids       text[] NOT NULL,
    parallels    integer[] NOT NULL,
    progress     bigint,
    max_progress bigint,
    seen_at      timestamptz NOT NULL
);

-- Cycles that ended, once each: (run, seq) is the agent's. Kept for
-- events-keep, as events.
CREATE TABLE factoryctl.pump_cycles (
    run      text NOT NULL,
    seq      bigint NOT NULL,
    module   text NOT NULL,
    batch    integer NOT NULL,
    result   text NOT NULL CHECK (result IN ('done', 'failed')),
    message  text NOT NULL DEFAULT '',
    duration double precision,
    ended_at timestamptz NOT NULL,  -- when the server heard of it
    PRIMARY KEY (run, seq)
);
CREATE INDEX pump_cycles_by_time ON factoryctl.pump_cycles (ended_at);

CREATE TABLE factoryctl.pump_cycle_slots (
    run      text NOT NULL,
    seq      bigint NOT NULL,
    slot     integer NOT NULL,
    fluid    text NOT NULL,
    parallel integer NOT NULL,
    expected bigint NOT NULL,  -- mB the cycle brings in this slot
    PRIMARY KEY (run, seq, slot),
    FOREIGN KEY (run, seq) REFERENCES factoryctl.pump_cycles ON DELETE CASCADE
);

-- The pumping network's stock of the target fluids, as last reported.
CREATE TABLE factoryctl.pump_stock (
    fluid  text PRIMARY KEY,
    amount bigint NOT NULL,
    at     timestamptz NOT NULL
);

-- QFT setups (factoryctl/qft): one Quantum Force Transformer, one computer, one
-- agent. It decides; the server keeps its settings and the last report. The
-- name is the key; the controller's coordinates (as first reported) tell two
-- setups given one name apart. JSON as the agent sends it: for the web.
CREATE TABLE factoryctl.qft_setups (
    name             text PRIMARY KEY,
    x                bigint,
    y                bigint,
    z                bigint,
    run              text NOT NULL,
    catalog          jsonb NOT NULL DEFAULT '[]',  -- its recipes
    status           jsonb NOT NULL DEFAULT '{}',  -- state, recipes, plasma
    seen_at          timestamptz NOT NULL,
    settings_version bigint NOT NULL DEFAULT 0,    -- 0: no settings yet
    settings         jsonb,                        -- mode, recipes
    settings_at      timestamptz
);

-- Mining setups (factoryctl/mining): one computer's mining modules. The server
-- plans them (domain::planMining), the agent carries the plan out. status: the
-- last report as the agent sent it (modules, pool, buffer, slots); plan: what
-- was sent back, per module ({module, setting, value, rates}), for the web and
-- for the other setups' plans.
CREATE TABLE factoryctl.mining_setups (
    name    text PRIMARY KEY,
    run     text NOT NULL,
    status  jsonb NOT NULL DEFAULT '{}',
    plan    jsonb NOT NULL DEFAULT '[]',
    seen_at timestamptz NOT NULL
);

-- What mining keeps stocked: a product (a dust, a fluid: goods of the stock)
-- and the module outputs it is made of ({name, damage}: they need not be
-- goods yet). priority as the pumps': urgency = 1 - fill^priority.
CREATE TABLE factoryctl.mining_targets (
    goods_id   bigint PRIMARY KEY REFERENCES factoryctl.goods ON DELETE CASCADE,
    threshold  bigint NOT NULL CHECK (threshold > 0),
    priority   double precision NOT NULL DEFAULT 1 CHECK (priority > 0),
    enabled    boolean NOT NULL DEFAULT true,
    ores       jsonb NOT NULL DEFAULT '[]',
    updated_at timestamptz NOT NULL DEFAULT now()
);
