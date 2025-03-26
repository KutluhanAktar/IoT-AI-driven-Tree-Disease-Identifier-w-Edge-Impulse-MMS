# IoT AI-driven Tree Disease Identifier w/ Edge Impulse & MMS
#
# LattePanda 3 Delta 864
#
# Detect tree diseases and get informed of the results via MMS
# to prevent them from spreading and harming forests, farms, and arable lands.
#
# By Kutluhan Aktar


import serial
import usb1
from PIL import Image
from io import BytesIO
from time import sleep
import cv2
import numpy as np
import datetime
from threading import Thread
import os
from edge_impulse_linux.image import ImageImpulseRunner
import requests
from twilio.rest import Client 

# Define the Grove - Vision AI Module image descriptions.
WEBUSB_JPEG_MAGIC = 0x2B2D2B2D
WEBUSB_TEXT_MAGIC = 0x0F100E12
# Define the Grove - Vision AI Module vendor and product IDs. 
VendorId = 0x2886
ProductId = 0x8060


class tree_disease_detection():
    def __init__(self, show_img, modelfile):
        # Define the required parameters for the Vision AI module.
        self.expect_size = 0
        self.buff = bytearray()
        self.show_img = show_img
        # Get data from the connected USB devices.
        self.context = usb1.USBContext()
        # Find the Vision AI module if connected.
        self.find_vision_ai(False)
        self.vision_ai_disconnect()
        # Initialize serial communication with Wio Terminal to get commands.
        self.wio = serial.Serial("/dev/ttyACM1", 115200, timeout=1000)
        # Define the Edge Impulse model settings.
        dir_path = os.path.dirname(os.path.realpath(__file__))
        self.modelfile = os.path.join(dir_path, modelfile)
        self.detection_result = ""
        # Define the required Twilio account settings.
        self.tw_account_sid = '<account_sid>' 
        self.tw_auth_token = '<auth_token>'
        self.tw_messaging_service_sid = '<messaging_service_sid>'
        self.verified_number = '+<verified_number>'
        # Define the PHP web application (image logger) settings.
        self.server = "www.theamplituhedron.com"
        self.url_path = "https://" + self.server + "/tree_disease_detection_web/"        

    def wio_commands(self):
        # Obtain commands from Wio Terminal via serial communication.
        command = self.wio.read(1).decode("utf-8")
        if(command.find("A") >= 0):
            self.vision_ai_save_sample()
        elif(command.find("B") >= 0):
            self.run_inference()
    
    def run_inference(self):
        # Run inference to detect potential tree diseases to prevent them from spreading.
        with ImageImpulseRunner(self.modelfile) as runner:
            try:
                # Print the information of the Edge Impulse model converted to a Linux x86_64 (64-bit) application (.eim).
                model_info = runner.init()
                print('Loaded runner for "' + model_info['project']['owner'] + ' / ' + model_info['project']['name'] + '"')
                labels = model_info['model_parameters']['labels']
                # Get the currently captured image with the Vision AI module, resize it depending on the given model, and run inference. 
                test_img = Image.open(BytesIO(self.captured_img))
                test_img = np.array(test_img)
                features, cropped = runner.get_features_from_image(test_img)
                res = runner.classify(features)
                # Obtain the prediction (detection) results for each label (class).
                results = "Detections: "
                if "bounding_boxes" in res["result"].keys():
                    print('Found %d bounding boxes (%d ms.)' % (len(res["result"]["bounding_boxes"]), res['timing']['dsp'] + res['timing']['classification']))
                    for bb in res["result"]["bounding_boxes"]:
                        # Get the detected labels:
                        results+="\n"+bb['label']
                        print('\t%s (%.2f): x=%d y=%d w=%d h=%d' % (bb['label'], bb['value'], bb['x'], bb['y'], bb['width'], bb['height']))
                        cropped = cv2.rectangle(cropped, (bb['x'], bb['y']), (bb['x'] + bb['width'], bb['y'] + bb['height']), (255, 0, 0), 1)
                # Save the resized (modified) image to the computer by appending the current date & time to its filename.
                date = datetime.datetime.now().strftime("%Y-%m-%d_%H_%M_%S")
                filename = 'detections/{}.jpg'.format(date)
                cv2.imwrite(filename, cv2.cvtColor(cropped, cv2.COLOR_RGB2BGR))
                # Then, send the saved image to the web application.
                self.send_captured_image(filename)
                # After uploading the image to the given server via the web application,
                # send an MMS to the verified phone number via Twilio so as to inform the user of the detection results.
                if not results == "Detections: ":
                    self.detection_result = "\n" + results
                else:
                    self.detection_result = "\nNot Detected!"                
                self.send_MMS_via_Twilio(self.detection_result, filename)
                
            # Stop the running inference.    
            finally:
                if(runner):
                    runner.stop()    
    
    def find_vision_ai(self, _open=True):
        print('*' * 50)
        print('Searching for Vision AI Module...')
        # Search all connected USB devices to find the Vision AI module. 
        for device in self.context.getDeviceIterator(skip_on_error=True):
            product_id = device.getProductID()
            vendor_id = device.getVendorID()
            device_addr = device.getDeviceAddress()
            bus = '->'.join(str(x) for x in ['Bus %03i' % (device.getBusNumber(),)] + device.getPortNumberList())
            # If the device vendor and product IDs correspond to the Vision AI module vendor and product IDs, start communicating with the Vision AI module.
            if vendor_id == VendorId and product_id == ProductId:
                print('\r' + f'\033[4;31mID {vendor_id:04x}:{product_id:04x} {bus} Device {device_addr} \033[0m',
                      end='')
                if _open:
                    return device.open()
                else:
                    device.close()
                    print(
                        '\r' + f'\033[4;31mID {vendor_id:04x}:{product_id:04x} {bus} Device {device_addr} CLOSED\033[0m',
                        flush=True)
            else:
                print(f'ID {vendor_id:04x}:{product_id:04x} {bus} Device {device_addr}')
                
    def vision_ai_connect(self):
        # Connect to the Vision AI module if found successfully.
        self.handle = self.find_vision_ai(True)
        if self.handle is None:
            print('\rPlease plug in the Vision AI Module!')
            return False
        with self.handle.claimInterface(2):
            # Set up the default Vision AI module settings to read data (buffer).
            self.handle.setInterfaceAltSetting(2, 0)
            self.handle.controlRead(0x01 << 5, request=0x22, value=0x01, index=2, length=2048, timeout=1000)
            print('\nVision AI Module is connected!')
        return True

    def vision_ai_disconnect(self):
        # Disconnect from the Vision AI module by resetting the module.
        try:
            print('Resetting device...')
            with usb1.USBContext() as context:
                handle = context.getByVendorIDAndProductID(VendorId, ProductId,
                                                           skip_on_error=False).open()
                handle.controlRead(0x01 << 5, request=0x22, value=0x00, index=2, length=2048, timeout=1000)
                handle.close()
                print('Device has been reset!')
            return True
        except:
            return False               

    def read_vision_ai_data(self):
        # Obtain the transferred data from the Vision AI module.
        with self.handle.claimInterface(2):
            # Utilize endpoints:
            self.handle.setInterfaceAltSetting(2, 0)
            self.handle.controlRead(0x01 << 5, request=0x22, value=0x01, index=2, length=2048, timeout=1000)
            # Save all transferred objects in a list so as to avoid any possible glitch.
            transfer_list = []
            for _ in range(1):
                transfer = self.handle.getTransfer()
                transfer.setBulk(usb1.ENDPOINT_IN | 2, 2048, callback=self.process_vision_ai_data, timeout=1000)
                transfer.submit()
                transfer_list.append(transfer)
            # Wait until one successful transfer.
            while any(x.isSubmitted() for x in transfer_list):
                self.context.handleEvents()

    def process_vision_ai_data(self, transfer):
        # If the Vision AI module transferred an object successfully, process the received data.
        if transfer.getStatus() != usb1.TRANSFER_COMPLETED:
            # transfer.close()
            return
        # Obtain the transferred data.
        data = transfer.getBuffer()[:transfer.getActualLength()]
        # Get the accurate buffer size.
        if len(data) == 8 and int.from_bytes(bytes(data[:4]), 'big') == WEBUSB_JPEG_MAGIC:
            self.expect_size = int.from_bytes(bytes(data[4:]), 'big')
            self.buff = bytearray()
        elif len(data) == 8 and int.from_bytes(bytes(data[:4]), 'big') == WEBUSB_TEXT_MAGIC:
            self.expect_size = int.from_bytes(bytes(data[4:]), 'big')
            self.buff = bytearray()
        else:
            self.buff = self.buff + data
        # If the obtained buffer size is equal to the actual buffer size, show the captured image on the screen.
        if self.expect_size == len(self.buff) and self.show_img:
            try:
                self.captured_img = self.buff
                img = Image.open(BytesIO(self.buff))
                img = np.array(img)
                cv2.imshow('Tree Disease Samples', cv2.cvtColor(img,cv2.COLOR_RGB2BGR))
                cv2.waitKey(1)
            except:
                self.buff = bytearray()
                return
        # Resubmit the transfer object after being processed.
        transfer.submit()
        
    def vision_ai_save_sample(self):    
        date = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = './samples/IMG_{}.jpg'.format(date)
        # If requested, save the recently captured image as a sample.
        with open(filename, 'wb') as f:
            f.write(bytes(self.captured_img))
        print("\nSaved: " + filename)

    def send_captured_image(self, file_path):
        files = {'captured_image': open("./"+file_path, 'rb')}
        # Make an HTTP POST request to the given web application to send the captured image.
        request = requests.post(self.url_path, files=files)
        print("\nRecently Captured Image Transferred!")
        # Print the response from the server.
        print("\nServer: " + request.text + "\n")
        
    def send_MMS_via_Twilio(self, body, file_path):
        # Define the Twilio client object.
        tw_client = Client(self.tw_account_sid, self.tw_auth_token)
        # Send an MMS to the verified phone number via Twilio.
        media_url = self.url_path + file_path
        message = tw_client.messages.create(
                                  messaging_service_sid=self.tw_messaging_service_sid, 
                                  body=body,
                                  media_url=media_url,
                                  to=self.verified_number
                              )
        print("\nTransferred Message ID:" + message.sid)
        print("Transferred Media URL:" + media_url)

# Define the detection object.
detection = tree_disease_detection(True, "model/tree-disease-identifier-linux-x86_64.eim")
detection.vision_ai_connect()

# Define and initialize threads.
def start_data_collection():
    while True:
        detection.read_vision_ai_data()
        
def activate_wio_commands():
    while True:
        detection.wio_commands()
        sleep(1)

Thread(target=start_data_collection).start()
Thread(target=activate_wio_commands).start()

    