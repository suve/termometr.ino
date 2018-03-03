/**
 * termometr.ino - an Arduino-based thermometer using DHT22 and ILI9341
 * Copyright (C) 2016-2018 Artur "suve" Iwicki
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program (LICENCE.txt). If not, see <http://www.gnu.org/licenses/>.
*/

// SPI.h is required by the ILI9341 library.
#include "SPI.h"

#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"

#include "DHT.h"

// For the Adafruit shield, these are the default.
#define TFT_RESET 9
#define TFT_DC    10
#define TFT_CS    8

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RESET);

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

// Total reads are 144 because because 144 / 24 gives 6 reads per hour, and 2px per read gives 288px, which fits nicely in 320px width.
// Min reads determine the max scale (6ph = 24 hours).
// Max reads determine the starting scale (192ph = 45min)
#define READS_TOTAL 144
#define READS_PER_HOUR_MIN  6
#define READS_PER_HOUR_MAX  768
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
unsigned long int next_store_millis;


#define GRAPH_READ_W 2
#define GRAPH_X_MARGIN (320 - ((READS_TOTAL) * GRAPH_READ_W) - 1)

#define GRAPH_Y_AREA 210

int graph_temp_max = +300;
int graph_temp_min = -200;
int graph_temp_h = 4;


void printTemperature(int temp, const int x, const int colour) {
  tft.fillRect(x, 0, FONT_W*3*7, FONT_H*3, ILI9341_BLACK);
  
  String text = (temp > 0) ? "+" : (temp < 0) ? "-" : "";
  temp = abs(temp);
  
  text += (temp / 10);
  text += ".";
  text += (temp % 10);
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

int calculateNewScale(void) {
  const int old_temp_max = graph_temp_max;
  const int old_temp_min = graph_temp_min;
  
  
  graph_temp_max = graph_temp_min = 0;
  
  
  for(int i = 0; i < in_count; ++i) {
    if(in_mem[i] > graph_temp_max)
      graph_temp_max = in_mem[i];
    else if(in_mem[i] < graph_temp_min)
      graph_temp_min = in_mem[i];
  }
  
  for(int o = 0; o < out_count; ++o) {
    if(out_mem[o] > graph_temp_max)
      graph_temp_max = out_mem[o];
    else if(out_mem[o] < graph_temp_min)
      graph_temp_min = out_mem[o];
  }
  
  
  // Make sure we include 0 on the scale
  if(graph_temp_max < 0) graph_temp_max = 0;
  if(graph_temp_min > 0) graph_temp_min = 0;
  
  // Round up or down to nearest 10 degrees (100 steps)
  if(graph_temp_max % 100) graph_temp_max = graph_temp_max - (graph_temp_max % 100) + 100;
  if(graph_temp_min % 100) graph_temp_min = graph_temp_min - (graph_temp_min % 100) - 100;
  
  // Just to be sure, when both min and max are 0 - force scale to [-10, +10]
  if((graph_temp_min == 0) && (graph_temp_max == 0)) {
    graph_temp_max = +100;
    graph_temp_min = -100;
  }
  
  graph_temp_h = GRAPH_Y_AREA / ((graph_temp_max/10) - (graph_temp_min/10));
  
  return ((graph_temp_max != old_temp_max) || (graph_temp_min != old_temp_min));
}

int temperatureToYPos(const int temp) {
  return 239 - (graph_temp_h * (temp - graph_temp_min))/10;
}

int graphMaxY(void) {
  return temperatureToYPos(graph_temp_max);
}

void drawScale(void) {
  tft.drawLine(GRAPH_X_MARGIN-1, graphMaxY(), GRAPH_X_MARGIN-1, 239, ILI9341_LIGHTGREY);
  
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(1);
  for(int temp = graph_temp_min; temp <= graph_temp_max; temp += 100) {
    String text = (temp > 0) ? "+" : "";
    text += temp / 10;
    
    int ypos = temperatureToYPos(temp);
    
    tft.setCursor(30 - 6*text.length(), ypos-4);
    tft.print(text);
  }
}

void drawTimeLabels(void) {
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(1);
  
  int ypos = graphMaxY() - FONT_H - 1;
  
  for(int i = 3; i > 0; --i) {
    int number; 
    char unit;
    if((READS_TOTAL / ReadsPerHour) >= 4) {
      number = (4-i) * (int)((10L * READS_TOTAL) / ReadsPerHour / 4);
      unit = 'h';
    } else {
      number = ((4-i) * (int)((10L * 60L * READS_TOTAL) / ReadsPerHour))/4;
      unit = 'm';
    }
    
    String text = "-";
    text += (number / 10);
    if(number % 10) { text += '.'; text += (number % 10); }
    text += unit;
    
    int xpos = GRAPH_X_MARGIN + 1 + GRAPH_READ_W*((i*READS_TOTAL)/4 - 1);
    xpos -= (text.length() * FONT_W)/2;
    
    tft.setCursor(xpos, ypos);
    tft.print(text);
  }
}

void drawAxes(void) {
  tft.setTextColor(ILI9341_LIGHTGREY);
  tft.setTextSize(1);
  
  // Horizontal lines so temperature levels are easily visible
  for(int temp = graph_temp_min; temp <= graph_temp_max; temp += 100) {
    int ypos = temperatureToYPos(temp);
    tft.drawLine(GRAPH_X_MARGIN, ypos, 319, ypos, (temp == 0) ? ILI9341_LIGHTGREY : ILI9341_DARKGREY);
  }
  
  // Vertical lines to easily check for temperature 6, 12 and 18 hours ago
  int ymax = graphMaxY();
  for(int i = 3; i > 0; --i) {
    int xpos = GRAPH_X_MARGIN + 1 + GRAPH_READ_W*((i*READS_TOTAL)/4 - 1);
    tft.drawLine(xpos, ymax, xpos, 239, ILI9341_DARKGREY);
  }
}


void drawLine(const int *const data, const int endsAt, const int count, const int colour) {
  if(count == 0) return;
  
  int start = endsAt - count + 1;
  if(start < 0) start += READS_TOTAL;
  
  int old_x = 320 - (count * GRAPH_READ_W);
  int old_y = temperatureToYPos(data[start]);
  
  if(count == 1) {
    tft.drawPixel(319, old_y, colour);
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

void drawGraph(const int fullRedraw) {
  if(fullRedraw) {
    tft.fillRect(0, 0, 320, 240, ILI9341_BLACK);
    drawScale();
  } else {
    int xpos = (in_count > out_count) ? in_count : out_count;
    xpos = 320 - (xpos * GRAPH_READ_W);
    
    tft.fillRect(xpos, FONT_H*3, 320, 240, ILI9341_BLACK);
  }
  
  drawAxes();
  drawTimeLabels();
  
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

int average(const int a, const int b) {
  if( ((a <= 0) && (b >= 0)) || ((a >= 0) && (b <= 0)) ) return (a+b)/2;
  
  int a_half = a / 2;
  int a_mod = a % 2;
  
  int b_half = b / 2;
  int b_mod = b % 2;
  
  return a_half + b_half + (a_mod & b_mod);
}

void storemem(const int current_temp, int *const mem, int *const index, int *const count) {
  if((*count == READS_TOTAL) && (ReadsPerHour > READS_PER_HOUR_MIN)) {
    for(int i = 0; i < READS_TOTAL/2; ++i) {
      mem[i] = average(mem[i*2], mem[i*2 +1]);
    }
    
    *count = READS_TOTAL/2;
    *index = *count-1;
  }
  
  *index = ((*index)+1) % READS_TOTAL;
  mem[*index] = current_temp;
  
  if(*count < READS_TOTAL) *count += 1; 
}

int storeTemperatues() {
  int old_in_count = in_count;
  int old_out_count = out_count;
  
  storemem(in_current, in_mem, &in_index, &in_count); 
  storemem(out_current, out_mem, &out_index, &out_count);  
  
  next_store_millis += STORE_DELAY;
  
  if((old_in_count > in_count) || (old_out_count > out_count)) {
    ReadsPerHour /= 2;
    return 1;
  }
  
  return 0;
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
  text = "Wersja 2017/0205";
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
  
  tft.setRotation(1);
  titlescreen();
  
  ReadsPerHour = READS_PER_HOUR_MAX;
  randomSeed(analogRead(0));
  delay(3500);
  
  tft.fillScreen(ILI9341_BLACK);
  drawScale();
  drawAxes();
  
  next_store_millis = millis();
}


#define LOOP_MIN_DELAY 4000

void loop(void) {
  unsigned long int current_millis = millis();
  
  readTemperatures();
  
  if((in_count == 0) || (out_count == 0) || (current_millis >= next_store_millis)) {
    int needsRedraw = 0;
    
    needsRedraw |= storeTemperatues();
    needsRedraw |= calculateNewScale();
    
    drawGraph(needsRedraw);
  }
  
  printCurrentTemperatures();
  
  // Depending on Y scale, the time labels may get overdrawn by the current temps.
  if(graphMaxY() - FONT_H - 1 < FONT_H*3) drawTimeLabels();
  
  
  if(next_store_millis - current_millis <= LOOP_MIN_DELAY*2)
    delay(next_store_millis - current_millis);
  else
    delay(LOOP_MIN_DELAY);
}
