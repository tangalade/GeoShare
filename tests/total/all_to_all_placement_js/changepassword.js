var user_name_g, cur_password_hash_g, new_password_hash_g;

requestChange = function(user_name,
			 cur_password_hash, new_password_hash)
{
    var error;

    xmlhttp = new XMLHttpRequest();

    var url = "http://54.173.39.128:8000/change" +
	"?user="       + user_name +
	"&oldpass="    + cur_password_hash +
	"&newpass="    + new_password_hash;
    //Status.concat(url + "<br>");

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
    var user_name_input = document.main_form.user_name_in,
    cur_password_input = document.main_form.cur_password_in,
    new_password_input = document.main_form.new_password_in;

    if ( user_name_input.value.length == 0 )
    {
	alert("User name required");
	return false;
    }
    user_name_g = user_name_input.value;
    if ( cur_password_input.value.length == 0 )
    {
	alert("Current password required");
	return false;
    }
    cur_password_hash_g = CryptoJS.SHA1(cur_password_input.value);
    if ( new_password_input.value.length == 0 )
    {
	alert("New password required");
	return false;
    }
    new_password_hash_g = CryptoJS.SHA1(new_password_input.value);

    var ret = requestChange(user_name_g,cur_password_hash_g,new_password_hash_g);
    error = ret[0];
    
    switch(error)
    {
    case 0:
	break;
    case 1:
	Status.concat("New password does not meet requirements." + "<br>");
	return false;
    case 2:
	Status.concat("Invalid username/current password combination." + "<br>");
	return false;
    case 3:
	Status.concat("Username does not exist." + "<br>");
	return false;
    default:
	Status.concat("Unknown error code: " + error + "<br>");
	return false;
    }
    
    Status.concat("Successfully changed password for user: " + user_name_g + "<br>");
    return true;
}
document.getElementById('submit_button').addEventListener('click',main);

Status.concat("Change initializations complete.<br>");
