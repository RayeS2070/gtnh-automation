-- The whole database of factoryctl. The data is not critical: a schema that
-- no longer fits is dropped and made again, there are no migrations.
-- Bump the version below on every change; the server refuses to start on a
-- database of another version and says how to drop it.

CREATE SCHEMA factoryctl;

CREATE TABLE factoryctl.schema_version (
    version integer NOT NULL
);
INSERT INTO factoryctl.schema_version VALUES (4);

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
CREATE TABLE factoryctl.stock (
    goods_id  bigint PRIMARY KEY REFERENCES factoryctl.goods ON DELETE CASCADE,
    amount    bigint NOT NULL,
    scan_id   bigint NOT NULL REFERENCES factoryctl.scans,
    probed_at timestamptz
);

-- What to keep stocked. threshold 0 = order whenever nothing is on order;
-- fill = order what is missing, in whole batches, instead of one batch.
CREATE TYPE factoryctl.priority AS ENUM ('high', 'low');

CREATE TABLE factoryctl.targets (
    goods_id   bigint PRIMARY KEY REFERENCES factoryctl.goods ON DELETE CASCADE,
    threshold  bigint NOT NULL CHECK (threshold >= 0),
    batch      bigint NOT NULL CHECK (batch > 0),
    priority   factoryctl.priority NOT NULL DEFAULT 'high',
    enabled    boolean NOT NULL DEFAULT true,
    note       text NOT NULL DEFAULT '',
    fill       boolean NOT NULL DEFAULT false,
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
