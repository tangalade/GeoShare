var user_name_g, password_hash_g;

requestCreate = function(user_name, password_hash)
{
    var error;

    xmlhttp = new XMLHttpRequest();

    var url = "http://54.173.39.128:8000/new" +
	"?user="       + user_name +
	"&pass="       + password_hash;
//    Status.concat(url + "<br>");

    // set to false if synchronous, but then you are stuck if server isn't running or something goes wrong with request
    xmlhttp.open("GET",url,false);
    xmlhttp.send();

    Status.concat("Received response: " + xmlhttp.responseText + "<br>");
    var jsonObj = JSON.parse(xmlhttp.responseText);
    error = jsonObj.error;
    
    return [error];
};

//Status.disable();

main = function ()
{
    var user_name_input = document.main_form.user_name_in,
    password_input = document.main_form.password_in;

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

    var ret = requestCreate(user_name_g,password_hash_g);
    error = ret[0];
    
    switch(error)
    {
    case 0:
	break;
    case 1:
	Status.concat("Password does not meet requirements." + "<br>");
	return false;
    case 2:
	Status.concat("Username already exists." + "<br>");
	return false;
    default:
	Status.concat("Unknown error code: " + error + "<br>");
	return false;
    }
	
    Status.concat("Successfully created user: " + user_name_g + "<br>");
    return true;
}
document.getElementById('submit_button').addEventListener('click',main);

Status.concat("Create initializations complete.<br>");
