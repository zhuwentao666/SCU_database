SELECT Id,ShipCountry,ShipRegion FROM "Order" WHERE ShipRegion in ('North America')
UNION
SELECT Id,ShipCountry, ("OtherPlace") as ShipRegion FROM "Order" WHERE ShipRegion not in ('North America')
LIMIT 20 OFFSET 15445-10248;