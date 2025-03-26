<?php

// If LattePanda 3 Delta transfers the captured image after running an inference with the Edge Impulse model successfully, save it to the detections folder.
if(!empty($_FILES["captured_image"]['name'])){
	// Image File:
	$captured_image_properties = array(
	    "name" => $_FILES["captured_image"]["name"],
	    "tmp_name" => $_FILES["captured_image"]["tmp_name"],
		"size" => $_FILES["captured_image"]["size"],
		"extension" => pathinfo($_FILES["captured_image"]["name"], PATHINFO_EXTENSION)
	);
	
    // Check whether the uploaded file extension is in the allowed file formats.
	$allowed_formats = array('jpg', 'png');
	if(!in_array($captured_image_properties["extension"], $allowed_formats)){
		echo 'FILE => File Format Not Allowed!';
	}else{
		// Check whether the uploaded file size exceeds the 5MB data limit.
		if($captured_image_properties["size"] > 5000000){
			echo "FILE => File size cannot exceed 5MB!";
		}else{
			// Save the uploaded file (image).
			move_uploaded_file($captured_image_properties["tmp_name"], "./detections/".$captured_image_properties["name"]);
			echo "FILE => Saved Successfully!";
		}
	}
}

?>