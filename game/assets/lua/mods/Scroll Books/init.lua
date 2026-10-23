-- Scroll Books (DETH)
-- Adria sells a Tome for Town Portal, Identify, Healing and Resurrect scrolls (3000 gold each).
-- A tome holds up to 50 scrolls of its kind: scrolls you pick up go into it, and you cast from it
-- like from a scroll (inventory or the spell list). The behaviour is built into the game.

local events = require("devilutionx.events")
local items = require("devilutionx.items")

events.ItemDataLoaded.add(function()
  items.addItemDataFromTsv("lua\\mods\\Scroll Books\\itemdat.tsv", 30000)
end)
