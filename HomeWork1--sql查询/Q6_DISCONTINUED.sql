SELECT ProductName,CompanyName,ContactName FROM (select ProductName,CustomerId as Id FROM(select ProductName,min(OrderDate) OrderDate from (SELECT ProductName,CustomerId,OrderDate FROM (SELECT OrderId as Id,ProductName FROM  "OrderDetail" AS "a1",(SELECT Id,ProductName FROM "Product" WHERE Discontinued>0) s1 WHERE s1.Id=ProductId) NATURAL JOIN "Order"
) GROUP BY ProductName) NATURAL JOIN (SELECT ProductName,CustomerId,OrderDate FROM (SELECT OrderId as Id,ProductName FROM  "OrderDetail" AS "a1",(SELECT Id,ProductName FROM "Product" WHERE Discontinued>0) s1 WHERE s1.Id=ProductId) NATURAL JOIN "Order"
)) NATURAL JOIN "Customer";