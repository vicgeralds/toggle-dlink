<!doctype html>
<html>
<head>
<meta charset="UTF-8">
<title>Control</title>
</head>
<body>
  Enable wireless
  <form action="" method="post">
    <input type="submit" value="on" name="active">
    <input type="submit" value="off" name="active">
  </form>
<?php

$json=file_get_contents("current_outputs.txt");
$out=json_decode($json,true);

if (isset($_REQUEST['active'])) {
  if ($_REQUEST['active']=="on") {;
    $out['active']=1;
  } else {
    $out['active']=0;
  }
  if (isset($out['count'])) {
    $out['count'] = ($out['count'] + 1) & 0x7FFF;
  } else {
    $out['count'] = 0;
  }
}

$newjson=json_encode($out);
file_put_contents("current_outputs.txt",$newjson);

echo file_get_contents("current_outputs.txt");
echo "<br>";
echo file_get_contents("current_readings.txt");

?>



</body>
</html>