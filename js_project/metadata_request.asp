<%
Response.Write("Hello.<br>")
dim bucketname, objname

objname=Request.QueryString("objname")
bucketname=Request.QueryString("bucketname")

Response.Write("Sorry, I don't have " & bucketname & "/" & objname & ".<br>")
%>