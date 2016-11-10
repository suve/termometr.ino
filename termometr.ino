
// SPI.h is required by the ILI9341 library.
#include "SPI.h"

#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"

#include "DHT.h"

// For the Adafruit shield, these are the default.
#define TFT_DC 9
#define TFT_CS 10

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

#define DHT_IN  2
#define DHT_OUT 3
DHT in_dht = DHT(DHT_IN, DHT22);
DHT out_dht = DHT(DHT_OUT, DHT22);


// According to Adafruit_GFX docs, font size is 5x8.
// Width macro is set to 6 instead, as strings seem to put a 1px spacing between characters.
#define FONT_W  6
#define FONT_H  8

#define COLOUR_IN   ILI9341_RED
#define COLOUR_OUT  ILI9341_DARKGREEN

// Total reads are 144 because I say so. (Okay, it's because 2px per read gives 288px which fits nicely in 320px width.)
// Min reads determine the max scale (6ph = 24 hours).
// Max reads determine the starting scale (384ph = 22.5min)
#define READS_TOTAL 144
#define READS_PER_HOUR_MIN  6
#define READS_PER_HOUR_MAX  384
unsigned long int ReadsPerHour;

int in_mem[READS_TOTAL];
int in_index = READS_TOTAL-1;
int in_count = 0;
int in_current;

int out_mem[READS_TOTAL];
int out_index = READS_TOTAL-1;
int out_count = 0;
int out_current;

#define STORE_DELAY ((60UL*60UL*1000UL)/ReadsPerHour)
unsigned long int last_store_millis;

#define GRAPH_X_MARGIN 32
#define GRAPH_Y_ZERO 159
#define GRAPH_READ_W 2
#define GRAPH_READ_H 4

void printTemperature(const int temp, const int x, const int colour) {
  tft.fillRect(x, 0, x+FONT_W*3*7, FONT_H*3, ILI9341_BLACK);
  
  String text = (temp > 0) ? "+" : "";
  text += (temp / 10);
  text += ".";
  text += (abs(temp) % 10);
  text += "C";

  tft.setCursor(x, 0);
  tft.setTextColor(colour);
  tft.print(text); 
}

void printCurrentTemperatures(void) {
  tft.setTextSize(3);
  
  printTemperature(in_current, GRAPH_X_MARGIN, COLOUR_IN);
  printTemperature(out_current, 176, COLOUR_OUT);
}

int temperatureToYPos(const int temp) {
  return GRAPH_Y_ZERO - (temp * GRAPH_READ_H / 10);
}

void drawScale(void) {
  tft.drawLine(GRAPH_X_MARGIN-1, 40, GRAPH_X_MARGIN-1, 239, ILI9341_LIGHTGREY);
  
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(1);
  for(int temp = -200; temp <= +300; temp += 100) {
    String text = (temp > 0) ? "+" : "";
    text += temp / 10;
    
    int ypos = temperatureToYPos(temp);
    
    tft.setCursor(30 - 6*text.length(), ypos-4);
    tft.print(text);
  }
}

void drawAxes(void) {
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(1);
  
  // Horizontal lines to temperature levels are easily visible
  for(int temp = -200; temp <= +300; temp += 100) {
    int ypos = temperatureToYPos(temp);
    tft.drawLine(GRAPH_X_MARGIN, ypos, 319, ypos, (temp == 0) ? ILI9341_LIGHTGREY : ILI9341_DARKGREY);
  }
  
  // Horizontal lines to easily check for temperature 6, 12 and 18 hours ago
  for(int i = -3; i < 0; i+=1) {
    int xpos = tft.width() -1 + i*GRAPH_READ_W*(READS_TOTAL/4);
    int ypos = temperatureToYPos(300);
    tft.drawLine(xpos, ypos, xpos, 239, ILI9341_DARKGREY);
    
    String text = "";
    if((READS_TOTAL / ReadsPerHour) >= 4) {
      text += i * (int)(READS_TOTAL / ReadsPerHour / 4);
      text += "h";
    } else {
      text += i * 15 * (int)(READS_TOTAL / ReadsPerHour);
      text += "m";
    }
    
    xpos -= (text.length() * FONT_W)/2;
    ypos -= (FONT_H + 1);
    tft.setCursor(xpos, ypos);
    tft.print(text);
  }
}


void drawLine(const int *const data, const int endsAt, const int count, const int colour) {
  if(count == 0) return;
  
  int start = endsAt - count + 1;
  if(start < 0) start += READS_TOTAL;
  
  int old_x = GRAPH_X_MARGIN + GRAPH_READ_W*(READS_TOTAL-count);
  int old_y = temperatureToYPos(data[start]);
  
  if(count == 1) {
    tft.drawPixel(old_x, old_y, colour);
    return;
  }
  
  for(int i = 1; i < count; ++i) {
    int index = (start + i) % READS_TOTAL;
    
    int new_x = old_x + GRAPH_READ_W;
    int new_y = temperatureToYPos(data[index]);
    
    tft.drawLine(old_x, old_y, new_x, new_y, colour);
    
    old_x = new_x;
    old_y = new_y;  
  }
}

void drawGraph() {
  tft.fillRect(GRAPH_X_MARGIN, FONT_H*3, 319, 239, ILI9341_BLACK);
  
  drawAxes();
  
  drawLine(in_mem, in_index, in_count, COLOUR_IN);
  drawLine(out_mem, out_index, out_count, COLOUR_OUT);
}


void readDHT(DHT *dht, int *const current_temp) {
  dht->read();
  *current_temp = dht->readTemperature() * 10.0;
}

void readTemperatures() {
  readDHT(&in_dht, &in_current); 
  readDHT(&out_dht, &out_current); 
}

void storemem(const int current_temp, int *const mem, int *const index, int *const count) {
  if((*count == READS_TOTAL) && (ReadsPerHour > READS_PER_HOUR_MIN)) {
    for(int i = 0; i < READS_TOTAL/2; ++i) {
      mem[i] = (mem[i] + mem[i+1]) / 2;
    }
    
    *count = READS_TOTAL/2;
    *index = *count-1;
  }
  
  *index = ((*index)+1) % READS_TOTAL;
  mem[*index] = current_temp;
  
  if(*count < READS_TOTAL) *count += 1; 
}

void storeTemperatues(const unsigned long int current_millis) {
  int old_in_count = in_count;
  int old_out_count = out_count;
  
  storemem(in_current, in_mem, &in_index, &in_count); 
  storemem(out_current, out_mem, &out_index, &out_count);  
  
  if((old_in_count > in_count) || (old_out_count > out_count)) {
    ReadsPerHour /= 2;
  }
  
  last_store_millis = current_millis;
}


void titlescreen() {
  tft.fillScreen(ILI9341_BLACK);
  
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_ORANGE);
  String text = "Stacja pogodowa";
  tft.setCursor((320 - 3*FONT_W*text.length())/2, 96);
  tft.print(text);
  
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW);
  text = "Wersja 2016/1110";
  tft.setCursor((320 - 2*FONT_W*text.length())/2, 124);
  tft.print(text);
  
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  
  text = "Oprogramowanie: suve";
  tft.setCursor((320 - FONT_W*text.length())/2, 220);
  tft.print(text);
  
  text = "Licencja: GNU GPL v3";
  tft.setCursor((320 - FONT_W*text.length())/2, 230);
  tft.print(text);
}


void setup() {
  in_dht.begin();
  out_dht.begin();
  tft.begin();
  
  tft.setRotation(3);
  titlescreen();
  
  ReadsPerHour = READS_PER_HOUR_MAX;
  randomSeed(analogRead(0));
  delay(3500);
  
  tft.fillScreen(ILI9341_BLACK);
  drawScale();
  drawAxes();
}


void loop(void) {
  readTemperatures();
  printCurrentTemperatures();
  
  unsigned long int current_millis = millis();
  if((in_count == 0) || (out_count == 0) || (current_millis - last_store_millis >= STORE_DELAY)) {
    storeTemperatues(current_millis);
    drawGraph();
  }
  
  delay(4800);
}
