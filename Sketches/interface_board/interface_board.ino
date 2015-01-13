// Bare-bones code for Primo.
// Note that #defines etc should really be in a *.h file shared between Primo and Cubetto.

// Sends a sequence of movement commands by radio, when the user-button is pressed.
// (See also cubetto_dom. Bare-bones code to receive and exexute movement commands).

// Uses Olimex 32u4 Leonardo boards, with nRf24l01 radio.

// Note that relaible radio performance requires that all connections to the radio use short wires
// and soldered joints.

// Pin-name Colour   Leonardo    nRF24L01

// MOSI     Blue     4(ICSP)     6
// MISO     Red      1(ICSP)     7
// SCK      Green    3(ICSP)     5
// CE       Orange   7(DIGITAL)  3
// CSN      Yellow   8(DIGITAL)  4
// IRQ      White    2(DIGITAL)  8
// GND      Black    GND(POWER)  1
// 3v3      Red      3v3(POWER)  2

// Note that these connections differ from the original RF24 library examples.
// (RF24 library must be installed).

// This code includes a mechanism for Primo/Cubetto to pair
// When a Cubetto is powered-on, it is unpaired and will accept input from any Primo.
// When a Primo is powered on, it generates a 32-bit random number to use as its Unique ID (UID).
// Every radio message that a Primo sends includes this UID.
// A Cubetto, on receiving its first radio message, records this UID,
// and subsequently ignores messages from any other UID.
// A devices's UID is forgotten when it is powered off.

// Normal usage of Primo/Cubetto:
// 1. Power-on Cubetto.
// 2. Send a message from Primo.
// 3. These devices are now paired.
// (4). Repeat 1 & 2 for any other Primos & Cubettos.
// (5). Power-off a Cubetto to un-pair (Primo is unaware of pairings, so no need to power-off).


#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"
#include <MCP23S17.h>


// version numbering
#define PRIMO_CUBETTO_PLAYSET_VERSION "1.0"

// define magnetic switches
#define PRIMO_MAGNET_NONE     0
#define PRIMO_MAGNET_FORWARD  1
#define PRIMO_MAGNET_RIGHT    2
#define PRIMO_MAGNET_LEFT     3
#define PRIMO_MAGNET_FUNCTION 4
#define PRIMO_MAGNET_ERR      5

void write_led(char led_number, char onoff);
char check_button(char button_to_check);

// define SS pins for GPIO expanders
#define PRIMO_GPIOEXP1_SS_PIN 9
#define PRIMO_GPIOEXP2_SS_PIN 10
#define PRIMO_GPIOEXP3_SS_PIN 11
#define PRIMO_GPIOEXP4_SS_PIN 12

// Instantiate Mcp23s17 objects
MCP23S17 gpioExp1(&SPI, PRIMO_GPIOEXP1_SS_PIN, 0);
MCP23S17 gpioExp2(&SPI, PRIMO_GPIOEXP2_SS_PIN, 0);
MCP23S17 gpioExp3(&SPI, PRIMO_GPIOEXP3_SS_PIN, 0);
MCP23S17 gpioExp4(&SPI, PRIMO_GPIOEXP4_SS_PIN, 0);

// Remove // comments from following line to enable debug tracing.
#define PRIMO_DEBUG_MODE 1

#ifdef PRIMO_DEBUG_MODE
#define debug_printf printf
#else
#define debug_printf(...) ((void) 0)
#endif

// The nRF24l01 can accept up to 32 bytes in a single radio packet,
// The comms protocol is designed to encapsulate a complete set of Primo movement commands
// in one packet along with an identifier for all Primo's, and a unique identifier for this Primo.
#define PRIMO_NRF24L01_MAX_PACKET_SIZE 32
static char packet[PRIMO_NRF24L01_MAX_PACKET_SIZE];

// This 32-bit value is the identifier for ANY 'Primo'.
// It is inserted into every radio packet sent by ANY Primo.
// A check is made on every radio packet, received by ANY Cubetto, that this value is present.
#define PRIMO_ID 0xDE1D8758   // NB This must be identical in Cubetto.

// This 32-bit value is randomly generated by THIS Primo every time it powers-up.
// It is inserted into every radio packet sent by THIS Primo.
// Cubetto uses this value to pair to THIS Primo.
static long primo_random = 0;

// Primo can generate a streem of up to 16 movement commands.
// Each command is 1 of Forward, Left or Right.
// Stop has been assigned to 0x00 to cover any empty slots in a Primo.
// One byte has been used for each movement command.
// This allows extra space for future meta-data (e.g. position ordinal),
// or extra commands (e.g. BACK, 45TURN etc).
#define PRIMO_COMMAND_STOP    0x00
#define PRIMO_COMMAND_FORWARD 0x01
#define PRIMO_COMMAND_LEFT    0x02
#define PRIMO_COMMAND_RIGHT   0x03


//
// Hardware configuration
//

// The Olimex 32u4 Leanoardo board user-button is defined here, because the original Leonardo
// board doesn't have a button by default and there is no pin definition for port E2 in the pins_arduino.h header
#define PRIMO_BBIT (PIND & B00000001) != 0    // Check if the button has been pressed 
#define PRIMO_BUTTONINPUT DDRD &= B11111110   // Initialize the port
// A better solution would be to have the user-button acting through an interrupt,
// as this would allow the Atmel uC (and the nRF24l01) to sleep between button-presses, and conserve battery power.

// Set up nRF24L01 radio on SPI bus plus pins 7 (CE) & 8 (CSN).
RF24 radio(7, 8);


//
// Topology
//

// Single radio pipe address for the 2 nodes to communicate.
const uint64_t pipe = 0xE8E8F0F0E1LL;

// Interrupt handler, check the radio because we got an IRQ
void checkRadio(void);

////////////////////////////////////////////////////////////////////////////////

void setup (void)
{
  // Initialize the user-button.
  PRIMO_BUTTONINPUT;


  //
  // Print preamble
  //

  Serial.begin(57600);
  printf_begin();

  //while (Serial.read() == -1)
  debug_printf("Cubetto Playset - Interface - Version %s\n\r", PRIMO_CUBETTO_PLAYSET_VERSION);


  //
  // Setup and configure rf radio
  //

  radio.begin();

  // We will be using the Ack Payload feature, so please enable it
  radio.enableAckPayload();

  // Open pipes to other nodes for communication
  radio.openWritingPipe(pipe);

  // Dump the configuration of the rf unit for debugging
  radio.printDetails();

  // Attach interrupt handler to interrupt #1 (using pin 2)
  // on BOTH the sender and receiver
  attachInterrupt(1, checkRadio, FALLING);


  // Set up all the GPIO expanders
  gpioExp1.begin();
  gpioExp2.begin();
  gpioExp3.begin();
  gpioExp4.begin();
  
  gpioExp1.pinMode(3, OUTPUT);
  gpioExp1.pinMode(7, OUTPUT);
  gpioExp1.pinMode(11, OUTPUT);
  gpioExp1.pinMode(15, OUTPUT);
  gpioExp2.pinMode(3, OUTPUT);
  gpioExp2.pinMode(7, OUTPUT);
  gpioExp2.pinMode(11, OUTPUT);
  gpioExp2.pinMode(15, OUTPUT);
  gpioExp3.pinMode(3, OUTPUT);
  gpioExp3.pinMode(7, OUTPUT);
  gpioExp3.pinMode(11, OUTPUT);
  gpioExp3.pinMode(15, OUTPUT);
  gpioExp4.pinMode(3, OUTPUT);
  gpioExp4.pinMode(7, OUTPUT);
  gpioExp4.pinMode(11, OUTPUT);
  gpioExp4.pinMode(15, OUTPUT);
  
  // This code always sends the same movement commands.
  initialise_packet();

  int led_loop, i;
  for (led_loop = 1; led_loop <= 3; led_loop++)
  {
    delay(100);

    // switch all LEDs on, delay for 100mS, then off
    for (i = 1; i <= 16; i++)
    {
      write_led(i, 1);
    } 

    delay(100);

    // switch all LEDs off
    for (i = 1; i <= 16; i++)
    {
      write_led(i, 0);
    } 
  }
}

////////////////////////////////////////////////////////////////////////////////

static uint32_t ackMessageCount = 0;
static char current_element;
static char function_element;
static char button;

int hall;
int terminated;
int i;
int movement_delay;

char led_fn_terminate;

#define PRIMO_MAX_BUTTON 28

////////////////////////////////////////////////////////////////////////////////

void loop (void)
{
  // Loop until the user-button pressed.
  //debug_printf("wait for !PRIMO_BBIT");
  //while(!PRIMO_BBIT)
  //{
  //  printf("PIND = %x\n\r", PIND);
  //  delay(1000);
  //}
  //debug_printf("!PRIMO_BBIT finished");

  // switch all LEDs off
  for (i = 1; i <= 16; i++)
  {
    write_led(i, 0);
  }

  while (PRIMO_BBIT)
  {
    for (button = 1; button <= 16; button++)
    {
      switch (check_button(button))
      {
        case PRIMO_MAGNET_FORWARD:
          write_led(button, 1);
          break;

        case PRIMO_MAGNET_RIGHT:
          write_led(button, 1);
          break;

        case PRIMO_MAGNET_LEFT:
          write_led(button, 1);
          break;     

        case PRIMO_MAGNET_FUNCTION:
          write_led(button, 1);
          break;

        case PRIMO_MAGNET_NONE:
          write_led(button, 0);
          break;

        default:
          break;
      }
    }
  }

  //debug_printf("Now sending packet\n\r");
  //radio.startWrite(packet, NRF24L01_MAX_PACKET_SIZE);
  //debug_printf("Finished sending\n\r");

  // Loop until the user-button is released - put LEDs on as magnets are fitted
  while(!PRIMO_BBIT) {}
 
  //
  // button is released, now calculate the packet to send
  // cycle through all the magnetic buttons, and fill in the packet appropriately
  //


  current_element = 8; // set to first movement element of the packet
  terminated = 0; // set at the first empty position to indicate end of the sequence#
  debug_printf("hall1 = %X\r\n", gpioExp1.readPort());
  debug_printf("hall2 = %X\r\n", gpioExp2.readPort());
  debug_printf("hall3 = %X\r\n", gpioExp3.readPort());
  debug_printf("hall4 = %X\r\n", gpioExp4.readPort());

  for (button = 1; button <= PRIMO_MAX_BUTTON; button++)
  {
    if (!terminated)
    {
      switch (check_button(button))
      {
        case PRIMO_MAGNET_FORWARD:
          packet[current_element++] = PRIMO_COMMAND_FORWARD;
          break;

        case PRIMO_MAGNET_RIGHT:
          packet[current_element++] = PRIMO_COMMAND_RIGHT;
          break;

        case PRIMO_MAGNET_LEFT:
          packet[current_element++] = PRIMO_COMMAND_LEFT; 
          break;     

        case PRIMO_MAGNET_FUNCTION:
          for (function_element = 1; function_element <= 4; function_element++)
          {
            switch (check_button(12 + function_element))
            {
              case PRIMO_MAGNET_FORWARD:
                packet[current_element++] = PRIMO_COMMAND_FORWARD;
                break;

              case PRIMO_MAGNET_RIGHT:
                packet[current_element++] = PRIMO_COMMAND_RIGHT;
                break;

              case PRIMO_MAGNET_LEFT:
                packet[current_element++] = PRIMO_COMMAND_LEFT; 
                break;

              default:
                break;
            }
          }
          break;

        case PRIMO_MAGNET_NONE:
          terminated = 1;
          break;

        default:
          break;
      }
    }
    else   // terminated
    {
      packet[current_element++] = PRIMO_COMMAND_STOP;
    }
  }

  // we have filled up the packet with commands, add stops to the end
  while (current_element < PRIMO_MAX_BUTTON)
    packet[current_element++] = PRIMO_COMMAND_STOP;

  debug_printf("Packet: ");

  for (i = 0; i < 24; i++)
  {
    debug_printf("%x ", packet[i]);
  }


  //TODO - why do I have to send twice? 

  debug_printf("\n\rNow sending packet\n\r");
  radio.startWrite(packet, PRIMO_NRF24L01_MAX_PACKET_SIZE);
  debug_printf("Finished sending\n\r");

  debug_printf("\n\rNow sending packet\n\r");
  radio.startWrite(packet, PRIMO_NRF24L01_MAX_PACKET_SIZE);
  debug_printf("Finished sending\n\r");

  // start lighting LEDs while we wait for the packet to be processed
  //write_led(1, 1);
  //delay (500);
  //write_led(1, 0);

  terminated = 0;   // set at the first empty position to indicate end of the sequence
  led_fn_terminate = 0;

  for (button = 1; button <= 12; button++)
  {
    if (!terminated)
    {
      switch (check_button(button))
      {
        case PRIMO_MAGNET_FORWARD:
          write_led(button, 0);
          movement_delay = 4500; // delay moving forward
          break;

        case PRIMO_MAGNET_RIGHT:
          write_led(button, 0);
          movement_delay = 3000; // delay moving right
          break;

        case PRIMO_MAGNET_LEFT:
          write_led(button, 0);
          movement_delay = 3000; // delay moving left
          break;     

        case PRIMO_MAGNET_FUNCTION:
          write_led(button, 0);
          led_fn_terminate = 0;

          for (function_element = 1; function_element <= 4; function_element++)
          {
            if (!led_fn_terminate)
            {
              if (check_button(12 + function_element) == PRIMO_MAGNET_NONE)
                led_fn_terminate = 1;
              else
              {
                write_led(12 + function_element, 0);
                if (check_button(12 + function_element) == PRIMO_MAGNET_FORWARD)
                  delay (4500);
                else
                  delay (3000);   // TODO handle delays during functions properly
              }
            }
          }

          // turn function lights back on after function executed
          for (function_element = 1; function_element <= 4; function_element++)
          {
            if (check_button(12 + function_element) != PRIMO_MAGNET_NONE)
            write_led(12 + function_element, 1);
          }

          movement_delay = 0;
          break;

        case PRIMO_MAGNET_NONE:
          terminated = 1;
          break;

        default:
          break;
      }

      delay(movement_delay);
    }
  }
      
  // switch all LEDs off
  for (i = 1; i <= 16; i++)
  {
    write_led(i, 0);
  } 

  //delay(1000);   // For user-button de-bounce etc.
}

////////////////////////////////////////////////////////////////////////////////

void checkRadio (void)
{
  // What happened?
  bool tx, fail, rx;
  radio.whatHappened(tx, fail, rx);

  // Have we successfully transmitted?
  if (tx)
  {
    debug_printf("Send:OK\n\r");
  }

  // Have we failed to transmit?
  if (fail)
  {
    debug_printf("Send:Failed\n\r");
  }

  // Transmitter can power down for now, because
  // the transmission is done.
  if (tx || fail)
    radio.powerDown();

  // Did we receive a message?
  if (rx)
  {
    radio.read(&ackMessageCount, sizeof(ackMessageCount));
    debug_printf("Ack:%lu\n\r", ackMessageCount);
  }
}

////////////////////////////////////////////////////////////////////////////////

void initialise_packet (void)
{
  long primo_id = PRIMO_ID;

  // Set the random number that will be used to uniquely identify THIS primo.
  // Note that random() actually returns a pseudo-random sequence.
  // randomSeed() ensures that each device initialises its sequence
  // to a fairly random noise source
  randomSeed(analogRead(0));
  primo_random = random();
  
  // Set the universal Primo ID, and the unique ID for this primo into the packet.
  // These values can be re-used in every packet sent.
  // (The UID could be used to set the packet address in the radio, but this would 
  // make it necessary to un-pair Primo/Cubetto at BOTH ends).
  memcpy(&packet[0], (const char*) &primo_id, 4);
  memcpy(&packet[4], (const char*) &primo_random, 4);

  // Test command to execute a sequence of 16 movement commands,
  // to produce a figure-of-8 pattern.
  packet[8] = PRIMO_COMMAND_FORWARD;
  packet[9] = PRIMO_COMMAND_LEFT;
  packet[10] = PRIMO_COMMAND_FORWARD;
  packet[11] = PRIMO_COMMAND_LEFT;
  packet[12] = PRIMO_COMMAND_FORWARD;
  packet[13] = PRIMO_COMMAND_LEFT;
  packet[14] = PRIMO_COMMAND_FORWARD;
  packet[15] = PRIMO_COMMAND_RIGHT;
  packet[16] = PRIMO_COMMAND_FORWARD;
  packet[17] = PRIMO_COMMAND_RIGHT;
  packet[18] = PRIMO_COMMAND_FORWARD;
  packet[19] = PRIMO_COMMAND_RIGHT;
  packet[20] = PRIMO_COMMAND_FORWARD;
  packet[21] = PRIMO_COMMAND_RIGHT;
  packet[22] = PRIMO_COMMAND_FORWARD;
  packet[23] = PRIMO_COMMAND_LEFT;

  //packet[8] = PRIMO_COMMAND_FORWARD;
  //packet[9] = PRIMO_COMMAND_FORWARD;
  //packet[10] = PRIMO_COMMAND_FORWARD;
  //packet[11] = PRIMO_COMMAND_FORWARD;
  //packet[12] = PRIMO_COMMAND_FORWARD;
  //packet[13] = PRIMO_COMMAND_FORWARD;
  //packet[14] = PRIMO_COMMAND_FORWARD;
  //packet[15] = PRIMO_COMMAND_FORWARD;
  //packet[16] = PRIMO_COMMAND_FORWARD;
  //packet[17] = PRIMO_COMMAND_FORWARD;
  //packet[18] = PRIMO_COMMAND_FORWARD;
  //packet[19] = PRIMO_COMMAND_FORWARD;
  //packet[20] = PRIMO_COMMAND_FORWARD;
  //packet[21] = PRIMO_COMMAND_FORWARD;
  //packet[22] = PRIMO_COMMAND_FORWARD;
  //packet[23] = PRIMO_COMMAND_FORWARD;

  // Ensure any unused positions are empty,
  // as Cubetto doesn't know if this is the end of the list
  // or just a gap before more movement instructions.
  packet[24] = PRIMO_COMMAND_STOP; 
  packet[25] = PRIMO_COMMAND_STOP;
  packet[26] = PRIMO_COMMAND_STOP;
  packet[27] = PRIMO_COMMAND_STOP;
  packet[28] = PRIMO_COMMAND_STOP;
  packet[29] = PRIMO_COMMAND_STOP;
  packet[30] = PRIMO_COMMAND_STOP;
  packet[31] = PRIMO_COMMAND_STOP;
}

////////////////////////////////////////////////////////////////////////////////

char check_button (char button_to_check)
{
  // takes a button number (1-12 for standard buttons, 13-16 for the function buttons)
  
  // returns the function for that button - PRIMO_MAGNET_FORWARD, PRIMO_MAGNET_LEFT, PRIMO_MAGNET_RIGHT, PRIMO_MAGNET_FUNCTION or PRIMO_MAGNET_NONE
  
  // simplest way to process is in a case - only 16 possibilities!
  int hall_response;
  char button, t_button;
  char ret_val;
  char invert_magnet; // used for line 2, as the magnets are inserted 'upside down'
  char twisted; // one of the sensors is twisted - swap a couple of bits

  // debug_printf("Checking button\r\n");

  invert_magnet = 0;
  twisted = 0;

  switch (button_to_check)
  {
    case 1:
      hall_response = gpioExp1.readPort();
      button = (hall_response & 0x0F);
      break;

    case 2:
      hall_response = gpioExp1.readPort();
      button = (hall_response >> 4) & 0x0F;
      break;

    case 3:
      hall_response = gpioExp1.readPort();
      button = (hall_response >> 8) & 0x0F;
      break;

    case 4:
      hall_response = gpioExp1.readPort();
      button = (hall_response >> 12) & 0x0F;
      break;

    case 5:
      hall_response = gpioExp2.readPort();
      button = (hall_response) & 0x0F;
      invert_magnet = 1;
      break;

    case 6:
      hall_response = gpioExp2.readPort();
      button = (hall_response >> 4) & 0x0F;
      invert_magnet = 1;
      break;

    case 7:
      hall_response = gpioExp2.readPort();
      button = (hall_response >> 8) & 0x0F;
      invert_magnet = 1;
      break;

    case 8:
      hall_response = gpioExp2.readPort();
      button = (hall_response >> 12) & 0x0F;
      invert_magnet = 1;
      twisted = 1;
      break;

    case 9:
      hall_response = gpioExp3.readPort();
      button = (hall_response) & 0x0F;
      break;

    case 10:
      hall_response = gpioExp3.readPort();
      button = (hall_response >> 4) & 0x0F;
      break;

    case 11:
      hall_response = gpioExp3.readPort();
      button = (hall_response >> 8) & 0x0F;
      break;

    case 12:
      hall_response = gpioExp3.readPort();
      button = (hall_response >> 12) & 0x0F;
      break;

    case 13:
      hall_response = gpioExp4.readPort();
      button = (hall_response) & 0x0F;
      break;

    case 14:
      hall_response = gpioExp4.readPort();
      button = (hall_response >> 4) & 0x0F;
      break;

    case 15:
      hall_response = gpioExp4.readPort();
      button = (hall_response >> 8) & 0x0F;
      break;

    case 16:
      hall_response = gpioExp4.readPort();
      button = (hall_response >> 12) & 0x0F;
      break;

    default:
      hall_response = 0;
      button = 0;
      break;
    }

    // only bottom 3 bits have hall data
    button = button & 0x07;

    // untwist bits
    if (twisted)
    {
      t_button = ((button & 0x01) + ((button & 0x02) << 1) + ((button & 0x04) >> 1));
      //debug_printf("button %X, t_button is %X\n\r", button, t_button);
      button = t_button; 
    }

    debug_printf("read button %X, hall = %X, button is %X\n\r", button_to_check, hall_response, button);

    // convert to button codes
    switch (button)
    {
      case 2:
        ret_val = invert_magnet ? PRIMO_MAGNET_FUNCTION : PRIMO_MAGNET_FUNCTION;
        break;
      case 3:
        ret_val = invert_magnet ? PRIMO_MAGNET_RIGHT : PRIMO_MAGNET_FORWARD;
        break;
      case 4:
        ret_val = invert_magnet ? PRIMO_MAGNET_FUNCTION : PRIMO_MAGNET_NONE;
        break;
      case 5:
        ret_val = invert_magnet ? PRIMO_MAGNET_LEFT : PRIMO_MAGNET_LEFT;
        break;
      case 6:
        ret_val = invert_magnet ? PRIMO_MAGNET_FORWARD : PRIMO_MAGNET_RIGHT;
        break;
      case 7:
        ret_val = PRIMO_MAGNET_NONE;
        break;
      default:
        ret_val = PRIMO_MAGNET_NONE;
        break;
    }

  debug_printf("found button %X\n\r", ret_val);

  return ret_val;
}

////////////////////////////////////////////////////////////////////////////////

void write_led (char led_number, char onoff)
{
  // onoff = 1 to turn on, 0 for off
  
  //debug_printf("LED %x - %x\r\n",led_number,onoff);

  switch (led_number)
  {
    case 1:
      gpioExp1.digitalWrite(3, 1 - onoff);
      break;

    case 2:
      gpioExp1.digitalWrite(7, 1 - onoff);
      break;

    case 3:
      gpioExp1.digitalWrite(11, 1 - onoff);
      break;

    case 4:
      gpioExp1.digitalWrite(15, 1 - onoff);
      break;

    case 5:
      gpioExp2.digitalWrite(3, 1 - onoff);
      break;

    case 6:
      gpioExp2.digitalWrite(7, 1 - onoff);
      break;

    case 7:
      gpioExp2.digitalWrite(11, 1 - onoff);
      break;

    case 8:
      gpioExp2.digitalWrite(15, 1 - onoff);
      break;

    case 9:
      gpioExp3.digitalWrite(3, 1 - onoff);
      break;

    case 10:
      gpioExp3.digitalWrite(7, 1 - onoff);
      break;

    case 11:
      gpioExp3.digitalWrite(11, 1 - onoff);
      break;

    case 12:
      gpioExp3.digitalWrite(15, 1 - onoff);
      break;

    case 13:
      gpioExp4.digitalWrite(3, 1 - onoff);
      break;

    case 14:
      gpioExp4.digitalWrite(7, 1 - onoff);
      break;

    case 15:
      gpioExp4.digitalWrite(11, 1 - onoff);
      break;

    case 16:
      gpioExp4.digitalWrite(15, 1 - onoff);
      break;
  }
}

