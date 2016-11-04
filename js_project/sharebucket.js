var user_name_g, password_hash_g;
var bucket_name_g, shared_user_name_g;

requestShare = function(user_name,password_hash,
			            bucket_name, shared_user_name)
{
    var error;

    xmlhttp = new XMLHttpRequest();

    var url = "http://" + server_address_g +
	"/"             + share_prefix_g +
	"?user="        + user_name +
	"&pass="        + password_hash +
	"&bucketname="  + bucket_name +
	"&shareduser="  + shared_user_name;
//    Status.concat(url + "<br>");

    // set to false if synchronous, but then you are stuck if server isn't running or something goes wrong with request
    xmlhttp.open("GET",url,false);
    xmlhttp.send();

    //    Status.concat("Received response: " + xmlhttp.responseText + "<br>");
    var jsonObj = JSON.parse(xmlhttp.responseText);
    error = jsonObj.error;
    
    return [error];
};

//Status.disable();

main = function ()
{
    Status.update("");
    var user_name_input = document.main_form.user_name_in,
    password_input = document.main_form.password_in,
    bucket_name_input = document.main_form.bucket_name_in,
    shared_user_name_input = document.main_form.shared_user_name_in;

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
    if ( shared_user_name_input.value.length == 0 )
    {
	alert("Shared user name required");
	return false;
    }
    shared_user_name_g = shared_user_name_input.value;

    var ret = requestShare(user_name_g,password_hash_g,
                           bucket_name_g, shared_user_name_g);
    error = ret[0];
    
    switch(error)
    {
    case 0:
	break;
    case 1:
	Status.concat("Invalid username/password combination." + "<br>");
	return false;
    case 2:
	Status.concat("You do not have permission to modify this object." + "<br>");
	return false;
    default:
	Status.concat("Unknown error received: " + error + "<br>");
	return false;
    }
    
    Status.concat("Successfully shared bucket: " + bucket_name_g + " with user: " + shared_user_name_g + " (but only in the GeoShare db)<br>");
    return true;
}
document.getElementById('submit_button').addEventListener('click',main);

Status.concat("Share initializations complete.<br>");
