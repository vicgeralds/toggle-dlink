<?php
//the arduino is talking to this script both to report its inputs
//and to read instructions on what to do with outputs
$enabled=$_REQUEST['enabled'];
$current_readings="{\"enabled\":$enabled}";
file_put_contents("current_readings.txt",$current_readings);
$json=file_get_contents("current_outputs.txt");
echo $json;
?>