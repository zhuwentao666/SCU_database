select Id,OrderDate,OrderDate as Last_Date,"0" as Difference from "Order" where CustomerId="BLONP" and Id="16766"
UNION
select Id,OrderDate,LAG(OrderDate,1) OVER (ORDER BY OrderDate) AS Last_Date,ROUND((julianday(OrderDate)-(LAG(julianday(OrderDate),1) OVER (ORDER BY OrderDate))),2) Difference FROM (SELECT Id,OrderDate,julianday(OrderDate) FROM (select Id,OrderDate from "Order" where CustomerId="BLONP" Order By OrderDate LIMIT 10))
Order BY OrderDate LIMIT 10 OFFSET 1;
