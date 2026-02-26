
#include <deque>

#define red 0
#define green 1

using std::deque;
//using std::algorithm;//required to use "find" function

struct button_t {
	int buttonPin;
	int redLedPin;
	int greenLedPin;
  int id=0;

  bool potty = 0;
	bool handRaised = 0;
	int color[2] = {0,0}; //red, green 
  
};       

deque<button_t*> queue;

//sets pinModes and initializes color
void buttonInit(struct button_t &btn){
 	  
    pinMode(btn.buttonPin, INPUT_PULLUP); 
  	pinMode(btn.redLedPin,OUTPUT);
  	pinMode(btn.greenLedPin,OUTPUT);
    
  //set LED color to nothing // starting color value in declaration
   digitalWrite(btn.redLedPin,btn.color[0]);
   digitalWrite(btn.greenLedPin,btn.color[1]);
  } 

void onLeds(){
  for(int x=0;x<queue.size();x++){
    digitalWrite(queue[x]->redLedPin, queue[x]->color[0]);
    digitalWrite(queue[x]->greenLedPin, queue[x]->color[1]);
  }
}

//returns the index of a desired id, ex btn.id
int queueIndex(int ID){
  for (int i=0; i<queue.size(); i++){
    if (queue[i]->id == ID){
      return i;
    }
  }
  Serial.println("id not found");
  return -1;//if id not found 
}

void setQueue(){
  if(!queue.empty()){
    queue[0]->color[green] = 1;//sets the first buttons green to 1
    queue[0]->color[red] = 0;//turns off red

  if(queue.size()>1){//more than one button has been pressed so set red to waiting
    for(int x=1; x<queue.size();x++){
      queue[x]->color[green] = 0;//turns green off
      queue[x]->color[red] = 1;//turns red on
      }  
    }
  onLeds();
  }
}

//the & "passes by reference", without this , it changes member values of 
//the struct locally within the function, but we cannot access globally
void checkHand(struct button_t& btn){
//button is pressed
  if(digitalRead(btn.buttonPin) == 0){
   delay(250);//should include debounce protection in the future
//adds or removes entire button to or from queue
   if(!btn.handRaised){
    queue.push_back(&btn);//add btn to the queue
    setQueue();//assign colors based on their position in the queue
   }
   else{
    //turn off leds
    digitalWrite(btn.greenLedPin,0);
    digitalWrite(btn.redLedPin,0);
    
    if(!queue.empty()) {
      //if the hand was raised and btns are in the queueu
      if(btn.color[green] == 1){//btn that was pressed twice is active queue btn, it was green and pressed, remove from the queue bc question was answered
        queue.pop_front();
        setQueue();
      }
      else if(btn.color[red]==1){//if they were red and pressed the button,  kick them to the back of the quee
        Serial.println("button color is red");
        if(queueIndex(btn.id)!=-1){//if id is found in queue
        queue.erase(queue.begin() + queueIndex(btn.id));
        setQueue();
        }
      }
    }
   }
   btn.handRaised = !btn.handRaised;

    for(int x=0; x<queue.size();x++){
      Serial.print(queue[x]->id);
      Serial.print(",");      
      }  
    Serial.println(" "); 
  }
}




