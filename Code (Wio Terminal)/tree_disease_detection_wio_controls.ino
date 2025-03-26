       /////////////////////////////////////////////
      //       IoT AI-driven Tree Disease        //
     //     Identifier w/ Edge Impulse & MMS    //
    //             ---------------             //
   //              (Wio Terminal)             //
  //             by Kutluhan Aktar           //
 //                                         //
/////////////////////////////////////////////

//
// Detect tree diseases and get informed of the results via MMS to prevent them from spreading and harming forests, farms, and arable lands.
//
// For more information:
// https://www.theamplituhedron.com/projects/IoT_AI_driven_Tree_Disease_Identifier_w_Edge_Impulse_MMS
//
//
// Connections
// Wio Terminal :
//                                Grove - VOC and eCO2 Gas Sensor
// SDA --------------------------- SDA
// SCL --------------------------- SCL
//                                Grove - CO2 & Temperature & Humidity Sensor
// SDA --------------------------- SDA
// SCL --------------------------- SCL
//                                Grove - Soil Moisture Sensor
// A0  --------------------------- SIG


// Include the required libraries.
#include <SPI.h>
#include <Seeed_FS.h>
#include "SD/Seeed_SD.h"
#include "TFT_eSPI.h"
#include "Histogram.h"
#include "RawImage.h"
#include "sensirion_common.h"
#include "sgp30.h"
#include "SCD30.h"
#include "RTC_SAMD51.h"
#include "DateTime.h"

// Define the built-in TFT screen and the histogram settings.
TFT_Histogram histogram=TFT_Histogram();
TFT_eSPI tft = TFT_eSPI();

// Initialize the File class and define the file name: 
File myFile;
const char* data_file = "environmental_factors.csv";

// Define the environmental factor thresholds to inform the user of potential tree disease risks.
int thresholds[3][6] = {
                        {800,38,42,435,350,1500},
                        {830,35,45,435,375,1650},
                        {950,42,60,600,485,1735}
                       };

// Define the required settings for the Grove - VOC and eCO2 Gas Sensor.
s16 err;
u32 ah = 0;
u16 scaled_ethanol_signal, scaled_h2_signal, tvoc_ppb, co2_eq_ppm;

// Define the Grove - Soil Moisture Sensor signal pin.
#define moisture_sensor A0

// Define the built-in RTC module.
RTC_SAMD51 rtc;

// Define the data holders.
#define DEBUG 0
float co2_value, temp_value, humd_value;
int tvoc_value, co2_eq_value, moisture_value;
int column_w = 40;
int background_color = TFT_BLACK;
int w = TFT_HEIGHT, h = 60, offset = 5;
long timer = 0;
long model_timer = 0;

void setup(){
  Serial.begin(115200);

  // Configurable Buttons:
  pinMode(WIO_KEY_A, INPUT_PULLUP);
  pinMode(WIO_KEY_B, INPUT_PULLUP);
  pinMode(WIO_KEY_C, INPUT_PULLUP);
  // Buzzer:
  pinMode(WIO_BUZZER, OUTPUT);
  
  // Initialize the built-in RTC module.  Then, adjust the date & time as the compiled date & time.
  rtc.begin();
  DateTime now = DateTime(F(__DATE__), F(__TIME__));
  rtc.adjust(now);

  // Check the connection status between Wio Terminal and the SD card.
  if(!SD.begin(SDCARD_SS_PIN, SDCARD_SPI)) while (1);

  // Check the SGP probe status.
  while(sgp_probe() != STATUS_OK){
    if(DEBUG) Serial.println("VOC and eCO2 Gas Sensor => SGP probe failed!");
    while (1);
  }
  // Read the H2 and Ethanol signal with the VOC and eCO2 gas sensor.
  err = sgp_measure_signals_blocking_read(&scaled_ethanol_signal, &scaled_h2_signal);
  // Check the VOC and eCO2 gas sensor status after reading the signal.
  if(err == STATUS_OK){ if(DEBUG) Serial.println("VOC and eCO2 Gas Sensor => Signal acquired successfully!"); }
  else{ if(DEBUG) Serial.println("VOC and eCO2 Gas Sensor => Signal reading error!"); }
  // Set the default absolute humidity value - 13.000 g/m^3.
  sgp_set_absolute_humidity(13000);
  // Initiate the VOC and eCO2 gas sensor.
  err = sgp_iaq_init();

  // Initialize the Grove - CO2 & Temperature & Humidity Sensor.
  scd30.initialize();

  // Initiate the built-in TFT screen.
  tft.init();
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  // Create the histogram.
  histogram.initHistogram(&tft);
  histogram.formHistogram("a", 1, 10, column_w, TFT_RED);     // Column 1
  histogram.formHistogram("b", 2, 10, column_w, TFT_PINK);    // Column 2
  histogram.formHistogram("c", 3, 10, column_w, TFT_GREEN);   // Column 3
  histogram.formHistogram("d", 4, 10, column_w, TFT_BLUE);    // Column 4
  histogram.formHistogram("e", 5, 10, column_w, TFT_YELLOW);  // Column 5
  histogram.formHistogram("f", 6, 10, column_w, TFT_MAGENTA); // Column 6
  // Hide the histogram axes.
  histogram.notShowAxis();

  // Define and display the 8-bit images saved on the SD card:
  drawImage<uint8_t>("forest_disease.bmp", TFT_HEIGHT, 0);
   
}

void loop(){
  get_VOC_and_eCO2();
  get_co2_temp_humd();
  get_moisture();
  check_thresholds(10);

  // Every 1 minute, update the histogram and append the collected environmental factors to the CSV file on the SD card.
  if(millis() - timer > 60*1000 || timer == 0){
    // Display the histogram on the TFT screen.
    update_histogram();
    show_resize_histogram(TFT_WHITE, TFT_BLACK);
    // Save the collected environmental factors to the SD card.
    save_data_to_SD_Card();
    // Every 5 minutes, send the model run command ('B') automatically to LattePanda 3 Delta.
    if(millis() - model_timer > 5*60*1000){
      Serial.println("B"); delay(500);
      tft.fillRect(0, TFT_WIDTH-h, w, h, TFT_WHITE);
      tft.fillRect(offset, TFT_WIDTH-h+offset, w-2*offset, h-2*offset, TFT_BLACK);
      tft.setTextSize(2);
      tft.drawString("Model Running!", (w-14*12)/2, TFT_WIDTH-25-12);
      // Update the model timer.
      model_timer = millis();
    }
    // Update the timer.
    timer = millis();
  }

  // If the configurable button A is pressed, send the capture command ('A') to LattePanda 3 Delta.  
  if(digitalRead(WIO_KEY_A) == LOW){
    Serial.println("A"); delay(500);
    tft.fillRect(0, 0, w, h, TFT_WHITE);
    tft.fillRect(offset, offset, w-2*offset, h-2*offset, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Image Captured!", (w-15*12)/2, 23);
  }

  // If the configurable button B is pressed, send the model run command ('B') manually to LattePanda 3 Delta.
  if(digitalRead(WIO_KEY_B) == LOW){
    Serial.println("B"); delay(500);
    tft.fillRect(0, TFT_WIDTH-h, w, h, TFT_WHITE);
    tft.fillRect(offset, TFT_WIDTH-h+offset, w-2*offset, h-2*offset, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Model Running!", (w-14*12)/2, TFT_WIDTH-25-12);
  }
}

void update_histogram(){
  // Update histogram parameters with the collected data.
  histogram.changeParam(1, "a", co2_value/10, TFT_RED);
  histogram.changeParam(2, "b", temp_value, TFT_PINK);
  histogram.changeParam(3, "c", humd_value, TFT_GREEN);
  histogram.changeParam(4, "d", moisture_value/10, TFT_BLUE);
  histogram.changeParam(5, "e", tvoc_value, TFT_YELLOW);
  histogram.changeParam(6, "f", co2_eq_value/10, TFT_MAGENTA);
}

void show_resize_histogram(int text, int background){
  // Resize, place, and display the histogram on the TFT screen.
  histogram.shrinkShowHistogram(25, 45, 1.4, text, background, background);
  tft.setRotation(3);
  tft.setTextSize(1);
  tft.drawString("a:CO2 b:Temp c:Humd d:Mois e:tVOC f:CO2eq", 30, 5);
  delay(5000);
  // Set the background image.
  drawImage<uint8_t>("forest_disease.bmp", 0, 0);
  delay(2000);
}

void save_data_to_SD_Card(){
  // Open the given CSV file on the SD card in the APPEND file mode.
  // FILE MODES: WRITE, READ, APPEND
  myFile = SD.open(data_file, FILE_APPEND);
  // If the given file is opened successfully:
  if(myFile){
    if(DEBUG){ Serial.print("\nWriting to "); Serial.print(data_file); Serial.println("..."); }
    // Obtain the current date & time.
    DateTime now = rtc.now();
    String _date = String(now.year(), DEC) + "_" + String(now.month(), DEC) + "_" + String(now.day(), DEC) + "_" + String(now.hour(), DEC) + "_" + String(now.minute(), DEC) + "_" + String(now.second(), DEC);
    // Create the data record to be inserted as a new row: 
    String data_record = String(_date)
                         + "," + String(co2_value)
                         + "," + String(temp_value)
                         + "," + String(humd_value)
                         + "," + String(moisture_value)
                         + "," + String(tvoc_value)
                         + "," + String(co2_eq_value)
                       ; 
    // Append the data record:
    myFile.println(data_record);
    // Close the CSV file:
    myFile.close();
    if(DEBUG) Serial.println("Data saved successfully!\n");
    // Notify the user after appending the given data record successfully.
    tft.fillRect(0, 0, w, h, TFT_WHITE);
    tft.fillRect(offset, offset, w-2*offset, h-2*offset, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Data Stored!", (w-12*12)/2, 23);
  }else{
    // If Wio Terminal cannot open the given CSV file successfully:
    if(DEBUG) Serial.println("Wio Terminal cannot open the given CSV file!\n");
    tft.setTextSize(2);
    tft.drawString("Wio Terminal", 35, 10);
    tft.drawString("cannot open the file!", 35, 30);
  }
  // Exit and clear:
  delay(3000);
}

void get_VOC_and_eCO2(){
  // Get the VOC (Volatile Organic Compounds) and CO2eq (Carbon dioxide equivalent) measurements evaluated by the VOC and eCO2 gas sensor.
  s16 err = 0;
  u16 tvoc_ppb, co2_eq_ppm;
  err = sgp_measure_iaq_blocking_read(&tvoc_ppb, &co2_eq_ppm);
  if(err == STATUS_OK){
    tvoc_value = tvoc_ppb;
    co2_eq_value = co2_eq_ppm;
    if(DEBUG){ Serial.print("tVOC (Volatile Organic Compounds): "); Serial.print(tvoc_value); Serial.println(" ppb"); }
    if(DEBUG){ Serial.print("CO2eq (Carbon dioxide equivalent): "); Serial.print(co2_eq_value); Serial.println(" ppm\n"); }
  }else{
    if(DEBUG) Serial.println("VOC and eCO2 Gas Sensor => IAQ values reading error!\n");
  }
  delay(1000);  
}

void get_co2_temp_humd(){
  // Obtain the CO2, temperature, and humidity measurements generated by the CO2 & Temperature & Humidity sensor.
  float result[3] = {0};
  if(scd30.isAvailable()){
    scd30.getCarbonDioxideConcentration(result);
    co2_value = result[0];
    temp_value = result[1];
    humd_value = result[2];
    if(DEBUG){ Serial.print("CO2 (Carbon dioxide): "); Serial.print(co2_value); Serial.println(" ppm"); }
    if(DEBUG){ Serial.print("Temperature: "); Serial.print(temp_value); Serial.println(" ℃"); }
    if(DEBUG){ Serial.print("Humidity: "); Serial.print(result[2]); Serial.println(" %\n"); }
  }
  delay(1000);
} 

void get_moisture(){
  moisture_value = analogRead(moisture_sensor);
  if(DEBUG){ Serial.print("Moisture: "); Serial.print(moisture_value); Serial.println("\n"); }
}

void check_thresholds(int s){
  // If the collected environmental factors exceed the given thresholds, notify the user via the built-in buzzer.
  for(int i=0; i<3; i++){
    if(co2_value >= thresholds[i][0] && temp_value >= thresholds[i][1] && humd_value >= thresholds[i][2] && moisture_value >= thresholds[i][3] && tvoc_value >= thresholds[i][4] && co2_eq_value >= thresholds[i][5]){
      analogWrite(WIO_BUZZER, 128);
      if(DEBUG) Serial.println("\nPotential tree disease risk detected!\n");
      delay(s*1000);
      analogWrite(WIO_BUZZER, 0);
    }
  }
}
