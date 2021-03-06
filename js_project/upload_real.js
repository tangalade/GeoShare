var user_name_g, password_hash_g;
var bucket_name_g, obj_name_g;
var data_file_g, src_reg_g, override_g;
var data_g, old_placements_g, new_placements_g;

var encoding_g;
var num_shares_g = 5, threshold_g = 3;

var excluded_g = [];

var regions_g = [
    "use1",
    "usw1",
    "usw2",
    "euw1",
    "sae1",
    "apne1",
    "apse1",
    "apse2",
];

// maximum concurrent requests
var MAX_ACTIVE_REQUESTS = 8;
// how many requests are currently being processed
var active_requests_g = 0;
// how many completed requests had errors
var error_requests_g = 0;
// how many shares do not have any running requests
var remaining_shares_g = 0;

getPlacement = function(user_name, password_hash,
			bucket_name, obj_name, 
			scheme, threshold, num_shares,
			excluded, src_reg, override)
{
    var old_placements = [];
    var new_placements = [];
    var error;

    // run once we work out the server thing
    xmlhttp = new XMLHttpRequest();

    var url = "http://54.173.39.128:8000/info" +
	"?user="       + user_name +
	"&pass="       + password_hash +
	"&bucketname=" + bucket_name +
	"&objname="    + obj_name +
	"&scheme="     + scheme +
	"&thresh="     + threshold +
	"&num_shares=" + num_shares +
	"&excluded="   + excluded +
	"&src_reg="    + src_reg +
	"&override="   + override;
    Status.concat(url + "<br>");

    // set to false if synchronous, but then you are stuck if server isn't running or something goes wrong with request
    xmlhttp.open("GET",url,false);
    xmlhttp.send();

    Status.concat("Received response: " + xmlhttp.responseText + "<br>");
    var jsonObj = JSON.parse(xmlhttp.responseText);
    if ( jsonObj.new_buckets )
	for ( i=0; i<jsonObj.new_buckets.length; i++ )
	    new_placements[i] = {bucket: jsonObj.new_buckets[i],
				 name: jsonObj.new_objects[i]};
    if ( jsonObj.old_buckets )
	for ( i=0; i<jsonObj.old_buckets.length; i++ )
	    old_placements[i] = {bucket: jsonObj.old_buckets[i],
				 name: jsonObj.old_objects[i]};
    error = jsonObj.error;

//    for ( i=0; i<new_placements.length; i++ )
//	Status.concat("new_placements[" + i + "]: " + new_placements[i] + "<br>");
//    for ( i=0; i<old_placements.length; i++ )
//	Status.concat("old_placements[" + i + "]: " + old_placements[i] + "<br>");

    return [old_placements, new_placements, error];
};

importData = function(file)
{
    var reader = new FileReader();
    reader.onload = (function(file_ref) {
	onFileLoad(reader.result);
    });
    reader.readAsBinaryString(file);
};

deleteNextShare = function()
{
    if ( remaining_shares_g == 0 )
	return;
    var idx = num_shares_g - remaining_shares_g;
    var bucket = new AWS.S3({
	params: {Bucket: old_placements_g[idx].bucket},
    });
    var params = {
	Key: old_placements_g[idx].name,
	Body: data_g[idx],
    };
    var request = bucket.deleteObject(params);
    active_requests_g++;
    remaining_shares_g--;
    Status.concat("&nbsp;&nbsp;".concat(idx+1,": ",old_placements_g[idx].bucket,"/",old_placements_g[idx].name,"<br>"));
    request.
	on('success', function(resp) {
	    active_requests_g--;
	    if ( remaining_shares_g > 0 )
		deleteNextShare();
	    if ( active_requests_g == 0 )
	    {
		Status.concat("Finished deleting, " + error_requests_g + " failed requests<br>");
		error_requests_g = 0;
	    }
	}).
	on('error', function(err,resp) {
	    active_requests_g--;
	    error_requests_g++;
	    Status.concat("Error deleting...".concat(err,"<br>"));
	    if ( remaining_shares_g > 0 )
		deleteNextShare();
	    else if ( active_requests_g == 0 )
	    {
		Status.concat("Finished deleting, ".concat(error_requests_g," failed requests<br>"));
		error_requests_g = 0;
	    }
	});
    request.send();
};

deleteOldData = function()
{
    remaining_shares_g = old_placements_g.length;
    
    // TODO: can delete all shares from one bucket at the same time, maybe split like that
    Status.concat("Deleting...<br>");
    for ( i=0; i<MAX_ACTIVE_REQUESTS && remaining_shares_g>0; i++ )
	deleteNextShare();
};

uploadNextShare = function()
{
    if ( remaining_shares_g == 0 )
	return;
    var idx = num_shares_g - remaining_shares_g;
    var bucket = new AWS.S3({
	params: {Bucket: new_placements_g[idx].bucket},
    });
    var params = {
	Key: new_placements_g[idx].name,
	Body: data_g[idx],
    };
    var request = bucket.putObject(params);
    active_requests_g++;
    remaining_shares_g--;
    Status.concat("&nbsp;&nbsp;".concat(idx+1,": ",new_placements_g[idx].bucket,"/",new_placements_g[idx].name,"<br>"));
    request.
	on('success', function(resp) {
	    active_requests_g--;
	    if ( remaining_shares_g > 0 )
		uploadNextShare();
	    if ( active_requests_g == 0 )
	    {
		Status.concat("Finished uploading, " + error_requests_g + " failed requests<br>");
		if ( error_requests_g == 0 )
		    document.getElementById("submit_status").innerHTML = "Upload complete";
		else
		    document.getElementById("submit_status").innerHTML = "Upload complete, with errors";
		error_requests_g = 0;
	    }
	}).
	on('error', function(err,resp) {
	    active_requests_g--;
	    error_requests_g++;
	    Status.concat("Error uploading...".concat(err,"<br>"));
	    if ( remaining_shares_g > 0 )
		uploadNextShare();
	    else if ( active_requests_g == 0 )
	    {
		Status.concat("Finished uploading, ".concat(error_requests_g," failed requests<br>"));
		document.getElementById("submit_status").innerHTML = "Upload complete, with errors";
		error_requests_g = 0;
	    }
	});
    request.send();
};

// uploads 'num_shares_g' objects from 'data_g[]'
uploadData = function()
{
    remaining_shares_g = new_placements_g.length;
    
    Status.concat("Uploading...<br>");
    for ( i=0; i<MAX_ACTIVE_REQUESTS && remaining_shares_g>0; i++ )
	uploadNextShare();
};

// returns encoded value of 'data' based on 'encoding'
encodeData = function(data, encoding)
{
    start_g = Date.now();
    var enc = ["aoeu"];
    switch(encoding)
    {
    case "NONE":
	enc = [data];
	break;
    case "AES":
	enc = [encryptAES(data, aes_key_g)];
	break;
    case "SSS":
	enc = shamirsSecretShare(data, num_shares_g, threshold_g);
	var dec = shamirsSecretRecover([enc[0],enc[1],enc[2]], threshold_g);
	if ( data != dec )
	    Status.concat("They differ" + "<br>");
	data_up_g = enc;
	break;
    case "XOR":
	enc = xorSecretShare(data, num_shares_g);
	break;
    default:
	alert("Invalid encoding type: ".concat(encoding));
    }
    Status.concat("Encoding time: " + (Date.now()-start_g) + "ms<br>");
    return enc;
}

// called when imported data has finished being stored into 'data'
onFileLoad = function(data)
{
    data_g = encodeData(data, encoding_g);
    deleteOldData();
    uploadData();
};


// update appearance of encoding params section, relative to num_shares_in
numSharesGuideUpdate = function()
{
    document.getElementById('encoding_params_guide').innerHTML = "Total number of shares generated from the object.";
    numSharesFormUpdate();
};
numSharesFormUpdate = function()
{
    var num_shares_input = document.getElementById('num_shares_in');
    var threshold_input = document.getElementById('threshold_in');

    if ( threshold_input.disabled == true )
    {
	Elem.unError('num_shares_in');
	Elem.unError('threshold_in');
	return;
    }

    if ( num_shares_input.value < threshold_input.value )
    {
	Elem.error('num_shares_in');
	Elem.unError('threshold_in');
	document.getElementById('encoding_params_guide').innerHTML = "Number of shares < threshold";
    }
    else
    {
	Elem.unError('num_shares_in');
	Elem.unError('threshold_in');
    }
};
document.getElementById('num_shares_in').addEventListener('change', numSharesFormUpdate);
document.getElementById('num_shares_in').addEventListener('focus', numSharesGuideUpdate);
numSharesFormUpdate();

// update appearance of encoding params section, relative to threshold_in
thresholdGuideUpdate = function()
{
    document.getElementById('encoding_params_guide').innerHTML = "Threshold number of shares required to recover the object";
    thresholdFormUpdate();
};
thresholdFormUpdate = function()
{
    var num_shares_input = document.getElementById('num_shares_in');
    var threshold_input = document.getElementById('threshold_in');

    if ( num_shares_input.disabled == true )
    {
	Elem.unError('threshold_in');
	Elem.unError('num_shares_in');
	return;
    }

    if ( num_shares_input.value < threshold_input.value )
    {
	Elem.error('threshold_in');
	Elem.unError('num_shares_in');
	document.getElementById('encoding_params_guide').innerHTML = "Number of shares < threshold";
    }
    else
    {
	Elem.unError('threshold_in');
	Elem.unError('num_shares_in');
    }
};
document.getElementById('threshold_in').addEventListener('change', thresholdFormUpdate);
document.getElementById('threshold_in').addEventListener('focus', thresholdGuideUpdate);
thresholdFormUpdate();

// updates appearance of encoding section based on current encoding selection
encodingFormUpdate = function()
{
    var encoding_input = document.main_form.encoding_in.options[document.main_form.encoding_in.selectedIndex];
    var encoding = encoding_input.value;
    
    switch(encoding)
    {
    case "NONE":
	Elem.fuzz('threshold_in');
	Elem.fuzz('num_shares_in');
	document.getElementById('encoding_guide').innerHTML = "Encoding limits the cloud service or government requisitions from compromising your data.";
	break;
    case "AES":
	Elem.fuzz('threshold_in');
	Elem.fuzz('num_shares_in');
	document.getElementById('encoding_guide').innerHTML = "AES-256 encryption before storage.";
	break;
    case "SSS":
	Elem.unFuzz("threshold_in");
	Elem.unFuzz("num_shares_in");
	document.getElementById('encoding_guide').innerHTML = "Your object is converted into a custom number of objects, a threshold number of which are required for recovery.";
	break;
    case "XOR":
	Elem.fuzz('threshold_in');
	Elem.unFuzz('num_shares_in');
	document.getElementById('encoding_guide').innerHTML = "Your object is converted into a custom number of objects, all of which are required for recovery.";
	break;
    default:
	alert("Invalid encoding type: ".concat(encoding_g));
    }
    thresholdGuideUpdate();
    numSharesGuideUpdate();
};
document.getElementById('encoding_in').addEventListener('click', encodingFormUpdate);
document.getElementById('encoding_in').addEventListener('change', encodingFormUpdate);
encodingFormUpdate();


//Status.disable();

main = function ()
{
    var bucket_name_input = document.main_form.bucket_name_in,
    obj_name_input = document.main_form.obj_name_in,
    user_name_input = document.main_form.user_name_in,
    password_input = document.main_form.password_in,
    file_input = document.main_form.file_in,
    cred_input = document.main_form.cred_in,
    override_input = document.main_form.override_in,
    encoding_input = document.main_form.encoding_in.options[document.main_form.encoding_in.selectedIndex];

    if ( user_name_input.value.length == 0 )
    {
	alert("User name required");
	return false;
    }
    user_name_g = user_name_input.value;
    if ( password_input.value.length == 0 )
    {
	alert("Password required");
	return false;
    }
    password_hash_g = CryptoJS.SHA1(password_input.value);
    if ( bucket_name_input.value.length == 0 )
    {
	alert("Bucket name required");
	return false;
    }
    bucket_name_g = bucket_name_input.value;
    if ( obj_name_input.value.length == 0 )
    {
	alert("Object name required");
	return false;
    }
    obj_name_g = obj_name_input.value;
    if ( file_input.files.length == 0 )
    {
	alert("File required");
	return false;
    }
    data_file_g = file_input.files[0];
    if ( cred_input.files.length == 0 )
    {
	alert("Credentials csv required");
	//Status.concat("using default credentials" + "<br>");
	return false;
    }
    cred_file_g = cred_input.files[0];
    override_g = (override_input.checked ? 1 : 0);

    // get excluded regions
    var i=0;
    for ( j=0; j<regions_g.length; j++ )
    {
	if ( document.getElementById(regions_g[j]).checked )
	    excluded_g[i++] = regions_g[j];
    }
    var excluded_http = "";
    for ( i=0; i<excluded_g.length; i++ )
    {
	excluded_http += excluded_g[i] + ",";
    }
    excluded_http = excluded_http.substr(0,excluded_http.length-1);

    // TODO: how to get source region, prob using HTML Geolocation then tying it to an AWS region
    src_reg_g = "use1";

    data_g = [];

    encoding_g = encoding_input.value;
    threshold_g = parseInt(document.main_form.threshold_in.value);
    num_shares_g = parseInt(document.main_form.num_shares_in.value);
    switch(encoding_g)
    {
    case "NONE":
    case "AES":
	num_shares_g = 1;
	threshold_g = 1;
	break;
    case "XOR":
	threshold_g = num_shares_g;
	break;
    case "SSS":
	if ( num_shares_g < threshold_g )
	{
	    Status.update("Number of shares < threshold");
	    return false;
	}
	break;
    default:
    }

    importCred(cred_file_g);

    // only supported in Firefox with Javascript 1.7, called "Destructuring assignment"
//    [old_placements_g, new_placements_g, error] = getPlacement(user_name_g, password_hash_g,
//								bucket_name_g, obj_name_g,
//								encoding_g, threshold_g, num_shares_g,
//								excluded_http, src_reg_g, override_g);
    var ret = getPlacement(user_name_g, password_hash_g,
			   bucket_name_g, obj_name_g,
			   encoding_g, threshold_g, num_shares_g,
			   excluded_http, src_reg_g, override_g);
    old_placements_g = ret[0];
    new_placements_g = ret[1];
    error = ret[2];
    
    switch(error)
    {
    case 0:
	break;
    case 1:
	Status.concat("Object already exists, but override not set." + "<br>");
	return false;
    case 2:
	Status.concat("You do not have permission to modify this object." + "<br>");
	return false;
    case 3:
	Status.concat("Invalid username/password combination." + "<br>");
	return false;
    default:
	Status.concat("Unknown error received: " + error + "<br>");
	return false;
    }

    Manager.wait("importCred",importData,data_file_g);
    //importData(data_file_g);

    return true;
}
document.getElementById('submit_button').addEventListener('click',main);

Status.concat("Upload initializations complete.<br>");
