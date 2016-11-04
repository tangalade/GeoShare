
shamirsSecretShare = function(data, num_shares, threshold)
{
    var dataHex = secrets.str2hex(data);
    try {
	return secrets.share(dataHex, num_shares, threshold);
    }
    catch (err) {
	alert(err);
    }
};
shamirsSecretRecover = function(shares, threshold)
{
    if ( shares.length < threshold )
    {
	alert("not enough shares");
	return false;
    }

    var comb;
    try {
	comb = secrets.combine( shares.slice(0,threshold) );
    }
    catch (err) {
	alert(err);
    }
    comb = secrets.hex2str(comb);
    return comb;
};

xorSecretShare = function(data, num_shares)
{
    var dataHex = secrets.str2hex(data);
    var frags = new Array(num_shares);

    var xored = data;
    for ( i=0; i<num_shares-1; i++ )
    {
	var next = "";
	var gen = "";
	for ( j=0; j<data.length; j++ )
	{
	    var rand = Math.floor(Math.random()*256 + 1);
	    gen = gen.concat(String.fromCharCode(rand));
	    next = next.concat(String.fromCharCode(xored.charCodeAt(j) ^ rand));
	}
	frags[i] = gen;
	xored = next;
    }
    frags[num_shares-1] = xored;
    return frags;
};
xorSecretRecover = function(data, num_shares)
{
    var secret = data[0];
    for ( i=1; i<data.length; i++ )
    {
	var next = "";
	for ( j=0; j<secret.length; j++ )
	{
	    next = next.concat(String.fromCharCode(secret.charCodeAt(j) ^ data[i].charCodeAt(j)));
	}
	secret = next;
    }
    return secret;
};
