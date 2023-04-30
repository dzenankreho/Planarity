#include "mbed.h"
#include "stm32f413h_discovery_ts.h"
#include "stm32f413h_discovery_lcd.h"
#include "easy-connect.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"

#define NUMOFNODES 6
#define MAXNUMOFEDGES 12
#define NUMOFTHEMES 4
#define NUMOFPLAYERS 5
#define EPSILON 0.00001

TS_StateTypeDef TS_State = { 0 };

struct Edge {
    pPoint point1;
    pPoint point2;
};

struct Theme {
    uint16_t color1;
    uint16_t color2;
    uint16_t color3;
} themes[NUMOFTHEMES] = {{LCD_COLOR_WHITE, LCD_COLOR_BLACK, LCD_COLOR_BLUE}, 
            {LCD_COLOR_DARKGRAY, LCD_COLOR_WHITE, LCD_COLOR_LIGHTRED},
            {LCD_COLOR_LIGHTRED, LCD_COLOR_DARKBLUE, LCD_COLOR_YELLOW},
            {LCD_COLOR_DARKGRAY, LCD_COLOR_WHITE, LCD_COLOR_LIGHTBLUE}};

struct PlayerHighscore {
    int classic;
    int race_against_time;
    int crazy;
} players_highscores[NUMOFPLAYERS] = {{-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};

int theme_selected = 0;
int num_of_moves = 0;
int t = 1;
int join_received = 0;
int num_of_edges = 0;
int current_player = 0;

Ticker ticker, ticker2;

bool go_to_ready = false;
bool start_host = false;
bool start_join = false;
bool lost = false;
bool host_join = false;

// Graph related functions
void DrawGraph();
int Orientation(Point p, Point q, Point r);
bool OnSegment(Point p, Point q, Point r);
bool DoIntersect(Point p1, Point q1, Point p2, Point q2);
int NumOfIntersections();
bool CheckConcurrent(int *a, int *b, int *c);
bool CheckParallel(int *a, int *b);
Point LineIntersection(int *a, int *b);

// Ticker functions
void ClassicTimer();
void RaceAgainstTimeTimer();
void RandomNodeChange();

// Main functionality functions
int MainScreen();
int Singleplayer(int gamemode);
int Gamemodes();
int ThemeSelection();
int Multiplayer();
int LevelSelection();
void GenerateGraph();
int PlayerSelection();
int Leaderboard();


// MQTT related functions
void MessageArrivedConnecting(MQTT::MessageData& md);
void MessageArrivedStart(MQTT::MessageData& md);
void MessageArrivedOpponent(MQTT::MessageData& md);
void MessageArrivedReceiveNodes(MQTT::MessageData& md);
void MessageArrivedReceiveConfirmation(MQTT::MessageData& md);

// Arrays that keeps the nodes and edges of currently generated graph
Point nodes[NUMOFNODES];
Edge edges[MAXNUMOFEDGES];

// Class taken from: https://stackoverflow.com/questions/5076695/how-can-i-iterate-through-every-possible-combination-of-n-playing-cards
class CombinationsIndexArray { 
    int index_array[3];         
    int size_of_index_array;
    int last_index;
    public:
    CombinationsIndexArray(int number_of_things_to_choose_from, int number_of_things_to_choose_in_one_combination) : size_of_index_array(number_of_things_to_choose_in_one_combination) {
        last_index = number_of_things_to_choose_from - 1;
        for (int i = 0; i < number_of_things_to_choose_in_one_combination; i++) {
            index_array[i];
        }
    }
    
    int operator[](int i) {
        return index_array[i];
    }
    
    int size() {
        return size_of_index_array;
    }
    
    bool advance() {
        int i = size_of_index_array - 1;
        if (index_array[i] < last_index) {
            index_array[i]++;
            return true;
        } else {
            while (i > 0 && index_array[i-1] == index_array[i]-1) {
                i--;
            }
            if (i == 0) {
                return false;
            } else {
                index_array[i-1]++;
                while (i < size_of_index_array) {
                    index_array[i] = index_array[i-1]+1;
                    i++;
                }
                return true;
            }
        }
    }
};

int main() {
    BSP_LCD_Init();

    if (BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize()) == TS_ERROR) {
        printf("BSP_TS_Init error\n");
    }

    int choice = 1, temp = 0;
    while (true) {
        wait(0.2); // Wait to prevent misclicks
        switch (choice) {
            case 1:
                choice = MainScreen();
                temp = 0;
                break;
            case 2:
                choice = Singleplayer(temp);
                temp = 0;
                break;
            case 3: 
                temp = Gamemodes();
                choice = (temp == 4) ? (1) : (2);
                break;
            case 4: 
                choice = Multiplayer();
                break;        
            case 5: 
                choice = Leaderboard();
                break;                        
            case 6: 
                choice = ThemeSelection();
                break;                        
            default:
                printf("Something went wrong!\n");
                return -1;
        }
    }
    
    return 0;
}

void DrawGraph() {
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    
    // Draw all edges
    for (Edge *p = edges; p < edges + num_of_edges; p++) {
        BSP_LCD_DrawLine(p->point1->X, p->point1->Y, p->point2->X, p->point2->Y);   
    }
    
    // Draw all nodes
    for (pPoint p = nodes; p < nodes + NUMOFNODES; p++) {
        BSP_LCD_SetTextColor((themes + theme_selected)->color3);
        BSP_LCD_FillCircle(p->X, p->Y, 5);
        BSP_LCD_SetTextColor((themes + theme_selected)->color2);
        BSP_LCD_DrawCircle(p->X, p->Y, 5);
    }
}

// Function taken from: https://www.geeksforgeeks.org/check-if-two-given-line-segments-intersect/
int Orientation(Point p, Point q, Point r) {
    int val = (q.Y - p.Y) * (r.X - q.X) -
              (q.X - p.X) * (r.Y - q.Y);
  
    if (val == 0) return 0;  // colinear
  
    return (val > 0) ? 1: 2; // clock or counterclock wise
}

// Function taken from: https://www.geeksforgeeks.org/check-if-two-given-line-segments-intersect/
bool OnSegment(Point p, Point q, Point r) {
    if (q.X <= max(p.X, r.X) && q.X >= min(p.X, r.X) &&
        q.Y <= max(p.Y, r.Y) && q.Y >= min(p.Y, r.Y))
       return true;
  
    return false;
}

// Function taken from: https://www.geeksforgeeks.org/check-if-two-given-line-segments-intersect/
bool DoIntersect(Point p1, Point q1, Point p2, Point q2) {
    // Find the four Orientations needed for general and
    // special cases
    int o1 = Orientation(p1, q1, p2);
    int o2 = Orientation(p1, q1, q2);
    int o3 = Orientation(p2, q2, p1);
    int o4 = Orientation(p2, q2, q1);
  
    // General case
    if (o1 != o2 && o3 != o4)
        return true;
        
    // Special Cases
    // p1, q1 and p2 are colinear and p2 lies on segment p1q1
    if (o1 == 0 && OnSegment(p1, p2, q1)) return true;
  
    // p1, q1 and q2 are colinear and q2 lies on segment p1q1
    if (o2 == 0 && OnSegment(p1, q2, q1)) return true;
  
    // p2, q2 and p1 are colinear and p1 lies on segment p2q2
    if (o3 == 0 && OnSegment(p2, p1, q2)) return true;
  
     // p2, q2 and q1 are colinear and q1 lies on segment p2q2
    if (o4 == 0 && OnSegment(p2, q1, q2)) return true;
  
    return false; // Doesn't fall in any of the above cases
}

int NumOfIntersections() {
    int num_of_intersections = 0;
    
    for (Edge *p = edges; p < edges + num_of_edges; p++) {
        for (Edge *q = p + 1; q < edges + num_of_edges; q++) {
            // Do not count edges which hava a commen node
            if (p->point1 == q->point1 || p->point1 == q->point2 || p->point2 == q->point1 || p->point2 == q->point2) {
                continue;
            }
            num_of_intersections += DoIntersect(*(p->point1), *(p->point2), *(q->point1), *(q->point2));
        }  
    }
    
    return num_of_intersections;
}

void ClassicTimer() {
    char buffer_timer[50];
    sprintf(buffer_timer, "Time elapsed: %ds   ", ++t);
    
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DisplayStringAt(0, 24, (uint8_t *)buffer_timer, LEFT_MODE);
    
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_FillRect(140, 24, BSP_LCD_GetXSize() - 130, 12);
}

void RaceAgainstTimeTimer() {
    char buffer_timer[50];
    sprintf(buffer_timer, "Time remaining: %ds   ", --t);
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DisplayStringAt(0, 24, (uint8_t *)buffer_timer, LEFT_MODE);
    
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_FillRect(140, 24, BSP_LCD_GetXSize() - 130, 12);    
}

int Singleplayer(int gamemode) {
    int level;

    // Set initial time for timer
    if (gamemode == 1) {
        t = 0;
    } else if (gamemode == 2) {
        level = LevelSelection();
        if (level == -1) {
            return 1;
        }
        t = 60 * (4 - level);
    } else if (gamemode == 3) {
        level = LevelSelection();
        if (level == -1) {
            return 1;
        }
        t = 0;
    }
    
    GenerateGraph();
    
    // Draw graph and information 
    DrawGraph();
    char buffer[50];
    sprintf(buffer, "Number of line crossings: %d", NumOfIntersections());
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)buffer, LEFT_MODE);
    BSP_LCD_DisplayStringAt(0, 12, (uint8_t *)"Moves taken: 0", LEFT_MODE);
    if (gamemode == 1 || gamemode == 3) {
        BSP_LCD_DisplayStringAt(0, 24, (uint8_t *)"Time elapsed: 0s", LEFT_MODE);
    } else if (gamemode == 2) {
        char buffer_[50];
        sprintf(buffer_, "Time remaining: %ds", t);
        BSP_LCD_DisplayStringAt(0, 24, (uint8_t *)buffer_, LEFT_MODE);
    }
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
    BSP_LCD_FillPolygon(back, 3);
    
    // Set tickers
    if (gamemode == 1) {
        ticker.attach(ClassicTimer, 1);
    } else if (gamemode == 2) {
        ticker.attach(RaceAgainstTimeTimer, 1);
    } else if (gamemode == 3) {
        ticker.attach(ClassicTimer, 1);
        ticker2.attach(RandomNodeChange, 7 * (4 - level));
    }
    
    num_of_moves = 0;
    while (true) {
        if (gamemode == 2 && t == 0) {
                BSP_LCD_SetTextColor((themes + theme_selected)->color3);
                BSP_LCD_SetBackColor((themes + theme_selected)->color1);
                BSP_LCD_SetFont(&Font12);
                BSP_LCD_DisplayStringAt(0, 227, (uint8_t *)"You ran out of time! :(", CENTER_MODE);
                ticker.detach();
        }      
        
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];

            if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                ticker.detach();
                ticker2.detach();
                break;
            }            
            
            for (pPoint p = nodes; p < nodes + NUMOFNODES; p++) {
                // Check if the pressed point is part of some node 
                if ((x - p->X) * (x - p->X) + (y - p->Y) * (y - p->Y) <= 25) {
                    if (NumOfIntersections() != 0) {
                        num_of_moves++;
                    }
                    
                    while (true) {
                        BSP_TS_GetState(&TS_State); 
                        if (!TS_State.touchDetected) {
                           break;
                        }                        
                        
                        x = TS_State.touchX[0];
                        y = TS_State.touchY[0];
                        
                        // Check if the new node is on the screen 
                        if (x >= 5 && x <= 234 && y >= 41 && y <= 234) {
                            // Draw graph with moved point
                            p->X = x;
                            p->Y = y;
                            DrawGraph();
                            
                            // Draw back button
                            BSP_LCD_SetTextColor((themes + theme_selected)->color3);
                            BSP_LCD_FillRect(219, 0, 20, 20);
                            BSP_LCD_SetTextColor((themes + theme_selected)->color2);
                            BSP_LCD_DrawRect(219, 0, 20, 20);
                            BSP_LCD_SetTextColor((themes + theme_selected)->color1);
                            Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
                            BSP_LCD_FillPolygon(back, 3);
                            
                            // Chech whether the puzzle is solved
                            int num_of_intersections = NumOfIntersections();
                            if (num_of_intersections == 0) {
                                ticker.detach();
                                ticker2.detach();
                                BSP_LCD_SetTextColor((themes + theme_selected)->color3);
                                BSP_LCD_SetBackColor((themes + theme_selected)->color1);
                                BSP_LCD_SetFont(&Font12);
                                BSP_LCD_DisplayStringAt(0, 227, (uint8_t *)"You have solved the puzzle! :)", CENTER_MODE);

                                // Calculate score and update highscore if necessary
                                if(gamemode == 1) {
                                    int score = t + num_of_moves;
                                    if ((players_highscores + current_player)->classic == -1 || (players_highscores + current_player)->classic > score) {
                                        (players_highscores + current_player)->classic = score;
                                    }
                                } else if (gamemode == 2) {
                                    int score = (60 + num_of_moves) * (4 - level) - t;
                                    if ((players_highscores + current_player)->race_against_time == -1 || (players_highscores + current_player)->race_against_time > score) {
                                        (players_highscores + current_player)->race_against_time = score;
                                    }
                                }else if (gamemode == 3) {
                                    int score = t + num_of_moves * (4 - level);
                                    if ((players_highscores + current_player)->crazy == -1 || (players_highscores + current_player)->crazy > score) {
                                        (players_highscores + current_player)->crazy = score;
                                    }
                                }                            
                            }
                            
                            // Print information
                            char buffer1[50], buffer2[50], buffer3[50];
                            sprintf(buffer1, "Number of line crossings: %d", num_of_intersections);
                            sprintf(buffer2, "Moves taken: %d", num_of_moves);
                            if (gamemode == 1 || gamemode == 3){
                                sprintf(buffer3, "Time elapsed: %ds", t);
                            } else if (gamemode == 2) {
                                sprintf(buffer3, "Time remaining: %ds", t);
                            }
                            BSP_LCD_SetFont(&Font12);
                            BSP_LCD_SetTextColor((themes + theme_selected)->color2);
                            BSP_LCD_SetBackColor((themes + theme_selected)->color1);
                            BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)buffer1, LEFT_MODE);
                            BSP_LCD_DisplayStringAt(0, 12, (uint8_t *)buffer2, LEFT_MODE);
                            BSP_LCD_DisplayStringAt(0, 24, (uint8_t *)buffer3, LEFT_MODE);
                        }
                    }
                    break;
                }
            }
            wait_us(1);
        }
    }
    
    return 1;
}

int MainScreen() {
    // Draw main screen
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"Planarity", CENTER_MODE);
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_DisplayStringAt(0, 227, (uint8_t *)"Ahmed Imamovic & Dzenan Kreho", CENTER_MODE);
    BSP_LCD_FillRect(53, 59, 132, 25);
    BSP_LCD_FillRect(53, 89, 132, 25);
    BSP_LCD_FillRect(53, 119, 132, 25);
    BSP_LCD_FillRect(53, 149, 132, 25);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_SetBackColor((themes + theme_selected)->color3);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(4, 65, (uint8_t *)"Singleplayer", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 95, (uint8_t *)"Multiplayer", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 125, (uint8_t *)"Leaderboard", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 155, (uint8_t *)"Change theme", CENTER_MODE);
    
    // Option selector
    while (true) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 53 && x <= 185 && y >= 59 && y <= 84) {
                return 3;
            } else if (x >= 53 && x <= 185 && y >= 89 && y <= 114) {
                return 4;
            } else if (x >= 53 && x <= 185 && y >= 119 && y <= 144) {
                return 5;
            } else if (x >= 53 && x <= 185 && y >= 149 && y <= 174) {
                return 6;
            }
        }
        wait_us(1);
    }
}

int Gamemodes() {
    // Select player
    int temp = PlayerSelection();
    if (temp != 0) {
        return temp;
    }
    
    // Draw gamemode screen
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"Select gamemode", CENTER_MODE);
    BSP_LCD_FillRect(23, 59, 192, 25);
    BSP_LCD_FillRect(23, 89, 192, 25);
    BSP_LCD_FillRect(23, 119, 192, 25);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_SetBackColor((themes + theme_selected)->color3);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(4, 65, (uint8_t *)"Classic", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 95, (uint8_t *)"Race against time", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 125, (uint8_t *)"Crazy", CENTER_MODE);
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
    BSP_LCD_FillPolygon(back, 3);
    
    
    // Option selector
    wait(0.5);
    while (true) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 23 && x <= 215 && y >= 59 && y <= 84) {
                return 1;
            } else if (x >= 23 && x <= 215 && y >= 89 && y <= 114) {
                return 2;
            } else if (x >= 23 && x <= 215 && y >= 119 && y <= 144) {
                return 3;
            } else if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                return 4;
            }            
        }
        wait_us(1);
    }
}

int LevelSelection() {
    // Draw level selection screen
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"Select difficulty", CENTER_MODE);
    BSP_LCD_FillRect(53, 59, 132, 25);
    BSP_LCD_FillRect(53, 89, 132, 25);
    BSP_LCD_FillRect(53, 119, 132, 25);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_SetBackColor((themes + theme_selected)->color3);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(4, 65, (uint8_t *)"Easy", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 95, (uint8_t *)"Normal", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 125, (uint8_t *)"Hard", CENTER_MODE);
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
    BSP_LCD_FillPolygon(back, 3);
    
    
    // Option selector
    wait(0.5);
    while (true) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 53 && x <= 185 && y >= 59 && y <= 84) {
                return 1;
            } else if (x >= 53 && x <= 185 && y >= 89 && y <= 114) {
                return 2;
            } else if (x >= 53 && x <= 185 && y >= 119 && y <= 144) {
                return 3;
            } else if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                return -1;
            }            
        }
        wait_us(1);
    }
}

void RandomNodeChange() {
    // Get random node and random coordinates
    int16_t random_node = rand() % (NUMOFNODES + 1); 
    int16_t random_x = rand() % 230 + 5;
    int16_t random_y = rand() % 194 + 41;
    
    (nodes + random_node)->X = random_x;
    (nodes + random_node)->Y = random_y;
    
    // Draw everything again because coordinates changed
    DrawGraph();
    
    // Print text information
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    char buffer[50];
    sprintf(buffer, "Number of line crossings: %d", NumOfIntersections());
    BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)buffer, LEFT_MODE);
    sprintf(buffer, "Moves taken: %d   ", num_of_moves);
    BSP_LCD_DisplayStringAt(0, 12, (uint8_t *)buffer, LEFT_MODE);
    sprintf(buffer, "Time elapsed: %ds   ", t);
    BSP_LCD_DisplayStringAt(0, 24, (uint8_t *)buffer, LEFT_MODE);
    
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_FillRect(200, 0, 18, 12);
    BSP_LCD_FillRect(120, 12, BSP_LCD_GetXSize() - 142, 12);
    BSP_LCD_FillRect(140, 24, BSP_LCD_GetXSize() - 130, 12);
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
    BSP_LCD_FillPolygon(back, 3);     
}

int ThemeSelection() {
    // Draw theme selection screen
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"Select theme", CENTER_MODE);
    for (int i = 0; i < NUMOFTHEMES; i++) {
        BSP_LCD_SetTextColor((themes + i)->color1);
        BSP_LCD_FillRect(53, 59 + 30 * i, 44, 25);
        BSP_LCD_SetTextColor((themes + i)->color2);
        BSP_LCD_FillRect(97, 59 + 30 * i, 44, 25);
        BSP_LCD_SetTextColor((themes + i)->color3);
        BSP_LCD_FillRect(141, 59 + 30 * i, 44, 25);
        BSP_LCD_SetTextColor((themes + theme_selected)->color2);
        BSP_LCD_DrawRect(53, 59 + 30 * i, 132, 25);
    }
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
    BSP_LCD_FillPolygon(back, 3);
    
    // Option selector
    wait(0.5);
    while (true) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 53 && x <= 185 && y >= 59 && y <= 84) {
                theme_selected = 0;
                break;
            } else if (x >= 53 && x <= 185 && y >= 89 && y <= 114) {
                theme_selected = 1;
                break;
            } else if (x >= 53 && x <= 185 && y >= 119 && y <= 144) {
                theme_selected = 2;
                break;
            } else if (x >= 53 && x <= 185 && y >= 149 && y <= 174) {
                theme_selected = 3;
                break;
            } else if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                break;
            }            
        }
        wait_us(1);
    }
    
    return 1;
}

int Multiplayer() {
    // Draw multiplayer screen
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"Select option", CENTER_MODE);
    BSP_LCD_FillRect(53, 59, 132, 25);
    BSP_LCD_FillRect(53, 89, 132, 25);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_SetBackColor((themes + theme_selected)->color3);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(4, 65, (uint8_t *)"Host", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 95, (uint8_t *)"Join", CENTER_MODE);
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
    BSP_LCD_FillPolygon(back, 3);
    
    // Option selector
    int choice = 0;
    wait(0.5);
    while (true) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 53 && x <= 185 && y >= 59 && y <= 84) {
                choice =  1;
                break;
            } else if (x >= 53 && x <= 185 && y >= 89 && y <= 114) {
                choice = 2;
                break;
            } else if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                choice = 3;
                break;
            }            
        }
        wait_us(1);
    }

    if (choice == 3) {
        return 1;
    }

    // Reset all relevant variables
    go_to_ready = false;
    start_host = false;
    start_join = false;
    lost = false;    
    
    // Determine is host or join selected 
    // host -> false
    // join -> true
    host_join = (choice == 1) ? (false) : (true);
    
    // Setup connection
    NetworkInterface *network;
    network = NetworkInterface::get_default_instance();
    MQTTNetwork mqttNetwork(network);
    MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);
    const char* hostname = "broker.hivemq.com";
    int port = 1883;
    int rc = mqttNetwork.connect(hostname, port);
    if (rc != 0) {
        printf("rc from TCP connect is %d\r\n", rc);
    }
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    if (choice == 1) {
        data.clientID.cstring = "host";
    } else {
        data.clientID.cstring = "join";
    }
    data.username.cstring = "";
    data.password.cstring = "";
    
    if ((rc = client.connect(data)) != 0) {
        printf("rc from MQTT connect is %d\r\n", rc);
    }

    MQTT::Message message;
    
    // Draw waiting screen
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 99, (uint8_t *)"Waiting for", CENTER_MODE);    
    BSP_LCD_DisplayStringAt(0, 119, (uint8_t *)"opponent", CENTER_MODE);
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_FillPolygon(back, 3);    
    
    bool back_button_pressed = false;
    
    // If join is selected send first message
    if (choice == 2) {
        char buf[] = "Join";
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)buf;
        message.payloadlen = strlen(buf);
        rc = client.publish("planarity/connecting", message);        
    }
    
    // Wait for someone to join
    while (!go_to_ready) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                back_button_pressed = true;
                break;
            }
        }        
        
        // Wait for initial message if host is selected or confirmation message if join is selected
        rc = client.subscribe("planarity/connecting", MQTT::QOS0, MessageArrivedConnecting);
        wait_us(1);
    }
    
    if (back_button_pressed) {
        return 4;
    }
    
    // If host is selected send reply/confirmation message
    if (choice == 1) {
        char buf[] = "Host";
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)buf;
        message.payloadlen = strlen(buf);
        rc = client.publish("planarity/connecting", message);            
    }

    // Draw start button
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_FillRect(82, 96, 73, 25);
    BSP_LCD_SetBackColor((themes + theme_selected)->color3);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 99, (uint8_t *)"START", CENTER_MODE);    
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_FillPolygon(back, 3);      
    
    // Wait for both host and join to press start
    back_button_pressed = false;
    while (!start_host || !start_join) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 82 && x <= 155 && y >= 96 && y <= 121) {
                if (choice == 1) {
                    start_host = true;
                } else {
                    start_join = true;
                }
                
                // Send message meaning this player pressed start
                char buf2[50];
                (choice == 1) ? (strcpy(buf2, "HostReady")) : (strcpy(buf2, "JoinReady"));
                message.qos = MQTT::QOS0;
                message.retained = false;
                message.dup = false;
                message.payload = (void*)buf2;
                message.payloadlen = strlen(buf2);
                rc = client.publish("planarity/connecting", message);                   
                
                // Draw waiting screen
                BSP_LCD_SetBackColor((themes + theme_selected)->color1);
                BSP_LCD_SetTextColor((themes + theme_selected)->color3);
                BSP_LCD_SetFont(&Font20);
                BSP_LCD_DisplayStringAt(0, 99, (uint8_t *)"Waiting for", CENTER_MODE);    
                BSP_LCD_DisplayStringAt(0, 119, (uint8_t *)"opponent", CENTER_MODE);                
            } else if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                back_button_pressed = true;
                break;
            }
        }        
        
        rc = client.subscribe("planarity/connecting", MQTT::QOS0, MessageArrivedStart);
        wait_us(1);
    }    
    
    if (back_button_pressed) {
        return 4;
    }    

    // Generate and send graph if host is selected or wait for and load received graph if join is selected
    GenerateGraph();
    join_received = 0;
    if (choice == 1) {
        wait(1);
        // Send nodes
        char sending_nodes[100];
        for (pPoint p = nodes; p < nodes + NUMOFNODES; p++) {
            char buf_temp[10];
            sprintf(buf_temp, "%d,%d;", p->X, p->Y);
            strcat(sending_nodes, buf_temp);
        }
        strcat(sending_nodes, "e");
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)sending_nodes;
        message.payloadlen = strlen(sending_nodes);
        rc = client.publish("planarity/connecting", message);

        // Wait for confirmation that join has loaded the nodes
        while (join_received != 1) {
            rc = client.subscribe("planarity/connecting", MQTT::QOS0, MessageArrivedReceiveConfirmation);
            wait_us(1);
        }
    
        // Send edges
        char sending_edges[100];
        for (Edge *p = edges; p < edges + num_of_edges; p++) {
            char buf_temp[10];
            sprintf(buf_temp, "%d,%d;", int(p->point1 - nodes), int(p->point2 - nodes));
            strcat(sending_edges, buf_temp);
        }
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)sending_edges;
        message.payloadlen = strlen(sending_edges);
        rc = client.publish("planarity/connecting", message);  
        
        // Wait for confirmation that join has loaded the edges
        while (join_received != 2) {
            rc = client.subscribe("planarity/connecting", MQTT::QOS0, MessageArrivedReceiveConfirmation);
            wait_us(1);            
        }
    } else {
        // Wait for nodes to arrive
        while(join_received != 1) {
            rc = client.subscribe("planarity/connecting", MQTT::QOS0, MessageArrivedReceiveNodes);
            wait_us(1);            
        }
        
        // Send confirmation that the nodes have arrived
        char buf_rec[] = "Received";
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)buf_rec;
        message.payloadlen = strlen(buf_rec);
        rc = client.publish("planarity/connecting", message);     
        
        // Wait for edges to arrive
        while(join_received != 2) {
            rc = client.subscribe("planarity/connecting", MQTT::QOS0, MessageArrivedReceiveNodes);
            wait_us(1);            
        }
        
        // Send confirmation that the edges have arrived
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void*)buf_rec;
        message.payloadlen = strlen(buf_rec);
        rc = client.publish("planarity/connecting", message);          
    }
    
    // Draw graph and information
    DrawGraph();
    char buffer[50];
    sprintf(buffer, "Number of line crossings: %d", NumOfIntersections());
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)buffer, LEFT_MODE);
    BSP_LCD_DisplayStringAt(0, 12, (uint8_t *)"Moves taken: 0", LEFT_MODE);
    BSP_LCD_DisplayStringAt(0, 24, (uint8_t *)"Time elapsed: 0s", LEFT_MODE);
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_FillPolygon(back, 3);
    
    // Set ticker
    t = 0;
    ticker.attach(ClassicTimer, 1);
    
    lost = false;
    while (true) {
        if (lost) {
                BSP_LCD_SetTextColor((themes + theme_selected)->color3);
                BSP_LCD_SetBackColor((themes + theme_selected)->color1);
                BSP_LCD_SetFont(&Font8);
                BSP_LCD_DisplayStringAt(0, 227, (uint8_t *)"Your opponent solved the puzzle. You lose :(", CENTER_MODE);
                ticker.detach();
        } 
        
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                ticker.detach();
                break;
            }            
            
            for (pPoint p = nodes; p < nodes + NUMOFNODES; p++) {
                // Check if the pressed point is part of some node 
                if ((x - p->X) * (x - p->X) + (y - p->Y) * (y - p->Y) <= 25) {
                    if (NumOfIntersections() != 0) {
                        num_of_moves++;
                    }
                    
                    while (true) {
                        BSP_TS_GetState(&TS_State); 
                        if (!TS_State.touchDetected) {
                           break;
                        }                        
                        
                        
                        x = TS_State.touchX[0];
                        y = TS_State.touchY[0];
                        
                        // Check if the new node is on the screen 
                        if (x >= 5 && x <= 234 && y >= 41 && y <= 234) {
                            // Draw graph with moved point
                            p->X = x;
                            p->Y = y;
                            DrawGraph();
                            
                            // Draw back button
                            BSP_LCD_SetTextColor((themes + theme_selected)->color3);
                            BSP_LCD_FillRect(219, 0, 20, 20);
                            BSP_LCD_SetTextColor((themes + theme_selected)->color2);
                            BSP_LCD_DrawRect(219, 0, 20, 20);
                            BSP_LCD_SetTextColor((themes + theme_selected)->color1);
                            //Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
                            BSP_LCD_FillPolygon(back, 3);
                            
                            // Chech whether the puzzle is solved
                            int num_of_intersections = NumOfIntersections();
                            if (num_of_intersections == 0) {
                                char buf3[50];
                                (choice == 1) ? (strcpy (buf3, "HostWon")) : (strcpy (buf3, "JoinWon"));
                                message.qos = MQTT::QOS0;
                                message.retained = false;
                                message.dup = false;
                                message.payload = (void*)buf3;
                                message.payloadlen = strlen(buf3);
                                rc = client.publish("planarity/connecting", message);                                 
                                
                                ticker.detach();
                                BSP_LCD_SetTextColor((themes + theme_selected)->color3);
                                BSP_LCD_SetBackColor((themes + theme_selected)->color1);
                                BSP_LCD_SetFont(&Font8);
                                BSP_LCD_DisplayStringAt(0, 227, (uint8_t *)"You have solved the puzzle. You win :)", CENTER_MODE);
                            }
                            
                            // Print text information
                            char buffer1[50], buffer2[50], buffer3[50];
                            sprintf(buffer1, "Number of line crossings: %d", num_of_intersections);
                            sprintf(buffer2, "Moves taken: %d", num_of_moves);
                            sprintf(buffer3, "Time elapsed: %ds", t);
                            BSP_LCD_SetFont(&Font12);
                            BSP_LCD_SetTextColor((themes + theme_selected)->color2);
                            BSP_LCD_SetBackColor((themes + theme_selected)->color1);
                            BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)buffer1, LEFT_MODE);
                            BSP_LCD_DisplayStringAt(0, 12, (uint8_t *)buffer2, LEFT_MODE);
                            BSP_LCD_DisplayStringAt(0, 24, (uint8_t *)buffer3, LEFT_MODE);
                        }
                    }
                    break;
                }
            }
            wait_us(1);
        }
        rc = client.subscribe("planarity/connecting", MQTT::QOS0, MessageArrivedOpponent);
    }
        
    return 4;
}

void MessageArrivedConnecting(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char* str=(char*)message.payload;
    
    if ((!host_join && !strncmp(str,"Join", 4)) || (host_join && !strncmp(str,"Host", 4))) {
        go_to_ready = true;
    }
}

void MessageArrivedStart(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char* str=(char*)message.payload;
    
    if (!host_join && !strncmp(str,"JoinReady", 9))  {
        start_join = true;
    } else if (host_join && !strncmp(str,"HostReady", 9)) {
        start_host = true;
    }
}

void MessageArrivedOpponent(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char* str=(char*)message.payload;
    
    if ((!host_join && !strncmp(str,"JoinWon", 7)) || (host_join && !strncmp(str,"HostWon", 7))){
        lost = true;
    }
}

void MessageArrivedReceiveNodes(MQTT::MessageData& md) {
    join_received++;
    MQTT::Message &message = md.message;
    char* str=(char*)message.payload;
    
    if (join_received == 1) {
        // Count number of nodes
        int count_num_of_nodes = 0;
        char *p = str;
        while (*p != '\0') {
            if (*p == ';') {
                count_num_of_nodes++;
            }
            p++;
        }
        
        // Find beginning of nodes
        p = str;
        while (*p != '\0') {
            if (*p >= '0' && *p <= '9') {
                break;
            }
            p++;
        }
        
        // Fill nodes
        for (int i = 0; i < count_num_of_nodes; i++) {
            int x, y;
            sscanf(p, "%d,%d;", &x, &y);
            (nodes + i)->X = x;
            (nodes + i)->Y = y;        
            while (*p != ';') {
                p++;
            }
            p++;
        }
    } else {
        // Count number of edges
        int count_num_of_edges = 0;
        char *p = str;
        while (*p != '\0' && *p != 'e') {
            if (*p == ';') {
                count_num_of_edges++;
            }
            p++;
        }
        
        // Find beginning of edges
        p = str;
        while (*p != '\0') {
            if (*p >= '0' && *p <= '9') {
                break;
            }
            p++;
        }
        
        // Fill edges
        for (int i = 0; i < count_num_of_edges; i++) {
            int p1, p2;
            sscanf(p, "%d, %d;", &p1, &p2);
            (edges + i)->point1 = (nodes + p1);
            (edges + i)->point2 = (nodes + p2);      
            while (*p != ';') {
                p++;
            }
            p++;
        }
    }
}

void MessageArrivedReceiveConfirmation(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char* str=(char*)message.payload;
    if (!strncmp(str,"Received", 8))  {
        join_received++;
    }   
}

void GenerateGraph() {
    int lines[4][3];
    
    // Set seed for randomisation
    static int num_of_times_called = 0;

    CombinationsIndexArray combos(4, 3);
    CombinationsIndexArray combosp(4, 2); 
    do {
        for(int i = 0; i < 4; i++){
            for(int j = 0; j < 3; j++){
                lines[i][j] = rand() % 10 + 1;
                printf("%d ", lines[i][j]); // This line does nothing but without it the simulator throws an exception
            }
        }
    } while ((!CheckConcurrent(lines[combos[0]],lines[combos[1]],lines[combos[2]])  &&
           !CheckParallel(lines[combosp[0]], lines[combosp[1]]) && combosp.advance()) || combos.advance() );

    // Calculate points of intersection
    Point intersections[3][3];
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < 3; j++) {
            if((i + j) < 3) {  
                intersections[i][j] = LineIntersection(lines[i], lines[j + i + 1]);
            }
        }
    }

    // Randomize points of intersection
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < 3; j++) {
            if((i + j) < 3) {    
                intersections[i][j].X = rand() % 230 + 5;
                intersections[i][j].Y = rand() % 194 + 41;
            }
        }
    }
    
    // Fill nodes array
    int temp = 0;
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < 3; j++) {
            if((i + j) < 3) {      
                nodes[temp++] = intersections[i][j];
            }
        }
    }
    
    // Get random number of edges between 10 and 12
    // for only 8 and 9 edges it is too easy to solve
    num_of_edges = rand() % 3 + 10;
    
    // Set default 9 edges
    edges[0].point1 = nodes + 0;
    edges[0].point2 = nodes + 1;
    
    edges[1].point1 = nodes + 1;
    edges[1].point2 = nodes + 2;
    
    edges[2].point1 = nodes + 0;
    edges[2].point2 = nodes + 4;
    
    edges[3].point1 = nodes + 4;
    edges[3].point2 = nodes + 3; 

    edges[4].point1 = nodes + 1;
    edges[4].point2 = nodes + 5;
    
    edges[5].point1 = nodes + 2;
    edges[5].point2 = nodes + 5;    
    
    edges[6].point1 = nodes + 4;
    edges[6].point2 = nodes + 5;     

    edges[7].point1 = nodes + 3;
    edges[7].point2 = nodes + 5;

    edges[8].point1 = nodes + 0;
    edges[8].point2 = nodes + 2;  
    
    // Add additional edges
    if (num_of_edges >= 10) {
        edges[9].point1 = nodes + 0;
        edges[9].point2 = nodes + 3;
    }
    if (num_of_edges >= 11) {
        edges[10].point1 = nodes + 4;
        edges[10].point2 = nodes + 1;  
    }
    if (num_of_edges == 12) {
        edges[11].point1 = nodes + 3;
        edges[11].point2 = nodes + 2;  
    }
}

bool CheckConcurrent(int *a, int *b, int *c) {
    return (c[0] * (a[1] * b[2] - b[1] * a[2]) +
            c[1] * (a[2] * b[0] - b[2] * a[0]) +
            c[2] * (a[0] * b[1] - b[0] * a[1]) == 0);
}

bool CheckParallel(int *a, int *b) {
    return abs((-((float)a[0] / (float)a[1])) - (-((float)b[0] / (float)b[1]))) < EPSILON;
}

Point LineIntersection(int *a, int *b) {
    return {-(a[2] * b[1] + a[1] * b[2]) / (a[0] * b[1] - b[0] * a[1]), 
            -(a[2] * b[0] + a[0] * b[2]) / (b[0] * a[1] - a[0] * b[1])};
}

int PlayerSelection() {
    // Draw level selection screen
    BSP_LCD_Clear((themes + theme_selected)->color1);
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_SetFont(&Font20);
    BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"Select player", CENTER_MODE);
    BSP_LCD_FillRect(53, 59, 132, 25);
    BSP_LCD_FillRect(53, 89, 132, 25);
    BSP_LCD_FillRect(53, 119, 132, 25);
    BSP_LCD_FillRect(53, 149, 132, 25);
    BSP_LCD_FillRect(53, 179, 132, 25);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    BSP_LCD_SetBackColor((themes + theme_selected)->color3);
    BSP_LCD_SetFont(&Font16);
    BSP_LCD_DisplayStringAt(4, 65, (uint8_t *)"Player 1", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 95, (uint8_t *)"Player 2", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 125, (uint8_t *)"Player 3", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 155, (uint8_t *)"Player 4", CENTER_MODE);
    BSP_LCD_DisplayStringAt(4, 185, (uint8_t *)"Player 5", CENTER_MODE);
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
    BSP_LCD_FillPolygon(back, 3);
    
    
    // Option selector
    wait(0.5);
    while (true) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 53 && x <= 185 && y >= 59 && y <= 84) {
                current_player = 0;
                return 0;
            } else if (x >= 53 && x <= 185 && y >= 89 && y <= 114) {
                current_player = 1;
                return 0;
            } else if (x >= 53 && x <= 185 && y >= 119 && y <= 144) {
                current_player = 2;
                return 0;
            } else if (x >= 53 && x <= 185 && y >= 149 && y <= 174) {
                current_player = 3;
                return 0;
            } else if (x >= 53 && x <= 185 && y >= 179 && y <= 204) {
                current_player = 4;
                return 0;
            } else if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                return 1;
            }            
        }
        wait_us(1);
    }
}

int Leaderboard() {
    // Draw leaderboard table
    BSP_LCD_Clear((themes + theme_selected)->color1);    
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_SetBackColor((themes + theme_selected)->color1);
    BSP_LCD_DisplayStringAt(10, 60, (uint8_t *)"P1", LEFT_MODE);
    BSP_LCD_DisplayStringAt(10, 90, (uint8_t *)"P2", LEFT_MODE);
    BSP_LCD_DisplayStringAt(10, 120, (uint8_t *)"P3", LEFT_MODE);
    BSP_LCD_DisplayStringAt(10, 150, (uint8_t *)"P4", LEFT_MODE);
    BSP_LCD_DisplayStringAt(10, 180, (uint8_t *)"P5", LEFT_MODE);
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_DisplayStringAt(50, 30, (uint8_t *)"Classic", LEFT_MODE);
    BSP_LCD_SetFont(&Font8);
    BSP_LCD_DisplayStringAt(108, 30, (uint8_t *)"Race against", LEFT_MODE);
    BSP_LCD_DisplayStringAt(125, 38, (uint8_t *)"time", LEFT_MODE);
    BSP_LCD_SetFont(&Font12);
    BSP_LCD_DisplayStringAt(175, 30, (uint8_t *)"Crazy", LEFT_MODE);
    BSP_LCD_SetFont(&Font16);

    // Find scores that arent -1 
    PlayerHighscore best_scores;
    for (int i = 0; i < 5; i++) {
        if ((players_highscores + i)->classic != -1) {
            best_scores.classic = (players_highscores + i)->classic;
        }
        if ((players_highscores + i)->race_against_time != -1) {
            best_scores.race_against_time = (players_highscores + i)->race_against_time;
        }
        if ((players_highscores + i)->crazy != -1) {
            best_scores.crazy = (players_highscores + i)->crazy;
        }        
    }
    
    // Find best scores
    for (int i = 0; i < 5; i++) {
        if ((players_highscores + i)->classic != -1 && (players_highscores + i)->classic < best_scores.classic) {
            best_scores.classic = (players_highscores + i)->classic;
        }
        if ((players_highscores + i)->race_against_time != -1 && (players_highscores + i)->race_against_time < best_scores.race_against_time) {
            best_scores.race_against_time = (players_highscores + i)->race_against_time;
        }
        if ((players_highscores + i)->crazy != -1 && (players_highscores + i)->crazy < best_scores.crazy) {
            best_scores.crazy = (players_highscores + i)->crazy;
        }        
    }

    // Print highscores to screen
    char buffer_score[10];
    for(int i = 0; i < 5; i++){
        BSP_LCD_SetTextColor((themes + theme_selected)->color3);
        BSP_LCD_SetBackColor((themes + theme_selected)->color1);        
        if ((players_highscores+i)->classic == -1) {
           BSP_LCD_DisplayStringAt(65, 60 + i * 30, (uint8_t *)"--", LEFT_MODE); 
        } else {
            sprintf(buffer_score, "%d", (players_highscores + i)->classic);
            if (best_scores.classic != -1 && (players_highscores + i)->classic == best_scores.classic) {
                BSP_LCD_SetTextColor((themes + theme_selected)->color1);
                BSP_LCD_SetBackColor((themes + theme_selected)->color3);
            }
            BSP_LCD_DisplayStringAt(65, 60 + i * 30, (uint8_t *)buffer_score, LEFT_MODE); 
        }
        BSP_LCD_SetTextColor((themes + theme_selected)->color3);
        BSP_LCD_SetBackColor((themes + theme_selected)->color1);  
        if ((players_highscores+i)->race_against_time == -1) {
            BSP_LCD_DisplayStringAt(125, 60 + i * 30, (uint8_t *)"--", LEFT_MODE); 
        } else {
            sprintf(buffer_score, "%d", (players_highscores+i)->race_against_time);
            if (best_scores.race_against_time != -1 && (players_highscores + i)->race_against_time == best_scores.race_against_time) {
                BSP_LCD_SetTextColor((themes + theme_selected)->color1);
                BSP_LCD_SetBackColor((themes + theme_selected)->color3);
            }         
            BSP_LCD_DisplayStringAt(125, 60 + i * 30, (uint8_t *)buffer_score, LEFT_MODE); 
        }
        BSP_LCD_SetTextColor((themes + theme_selected)->color3);
        BSP_LCD_SetBackColor((themes + theme_selected)->color1); 
        if ((players_highscores+i)->crazy == -1) {
            BSP_LCD_DisplayStringAt(180, 60 + i * 30, (uint8_t *)"--", LEFT_MODE); 
        } else {
            sprintf(buffer_score, "%d", (players_highscores+i)->crazy);
            if (best_scores.crazy != -1 && (players_highscores + i)->crazy == best_scores.crazy) {
                BSP_LCD_SetTextColor((themes + theme_selected)->color1);
                BSP_LCD_SetBackColor((themes + theme_selected)->color3);
            }
            BSP_LCD_DisplayStringAt(180, 60 + i * 30, (uint8_t *)buffer_score, LEFT_MODE);    
        }
    }    
    
    // Draw back button
    BSP_LCD_SetTextColor((themes + theme_selected)->color3);
    BSP_LCD_FillRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color2);
    BSP_LCD_DrawRect(219, 0, 20, 20);
    BSP_LCD_SetTextColor((themes + theme_selected)->color1);
    Point back[3] = {{224, 10}, {234, 4}, {234, 16}};
    BSP_LCD_FillPolygon(back, 3);
    
    // Wait for back button to be pressed
    wait(0.5);
    while (true) {
        BSP_TS_GetState(&TS_State);
        if(TS_State.touchDetected) {
            uint16_t x = TS_State.touchX[0];
            uint16_t y = TS_State.touchY[0];
            
            if (x >= 219 && x <= 239 && y >= 0 && y <= 20) {
                break;
            }            
        }
        wait_us(1);
    }
    
    return 1;
}