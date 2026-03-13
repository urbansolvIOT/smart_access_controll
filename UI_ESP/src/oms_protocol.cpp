#include "oms_protocol.h"

#define RXD2 22
#define TXD2 21

HardwareSerial OMS(2);

/* ===== UI CALLBACK ===== */

void update_pose();
void close_pose_popup();
void show_popup_success(const char *msg);
void show_popup_fail(const char *msg);
void ui_verify_success(const char *name);
void ui_verify_fail();
void show_status_popup(const char *msg);

/* ================= PROTOCOL ================= */

#define HDR1 0xEF
#define HDR2 0xAA

#define MID_RESET  0x10
#define MID_ENROLL 0x13
#define MID_VERIFY 0x12
#define MID_REPLY  0x00
#define MID_NOTE   0x01

bool enrolling=false;
bool verifying=false;
bool faceDetected = false;
unsigned long verifyCooldown = 0;
unsigned long verifyStart = 0;

char enrollName[32];
uint8_t enrollAdmin;

/* ================= POSE ORDER ================= */

uint8_t enrollStep=0;
const uint8_t poseList[5]={0x01,0x04,0x02,0x10,0x08};

/* ================= CHECKSUM XOR ================= */

uint8_t checksum(uint8_t mid,uint16_t len,uint8_t *data)
{
  uint8_t cs=0;

  cs^=mid;
  cs^=(len>>8);
  cs^=(len&0xFF);

  for(int i=0;i<len;i++)
    cs^=data[i];

  return cs;
}

/* ================= RESET ================= */

void sendReset()
{
  uint8_t pkt[]={0xEF,0xAA,0x10,0x00,0x00,0x10};
  OMS.write(pkt,sizeof(pkt));

  verifying=false;
  enrolling=false;
}

/* ================= ENROLL ================= */

void sendEnroll(uint8_t pose)
{
  uint8_t payload[35];
  memset(payload,0,35);

  payload[0] = enrollAdmin;

  memcpy(&payload[1], enrollName, strlen(enrollName));

  payload[33] = pose;
  payload[34] = 0x14;

  uint8_t pkt[80];
  int i=0;

  pkt[i++]=HDR1;
  pkt[i++]=HDR2;
  pkt[i++]=MID_ENROLL;
  pkt[i++]=0x00;
  pkt[i++]=0x23;

  for(int k=0;k<35;k++)
    pkt[i++]=payload[k];

  pkt[i++]=checksum(MID_ENROLL,35,payload);

  OMS.write(pkt,i);
}

/* ================= SET USER ================= */

void oms_set_user(const char *name, uint8_t admin)
{
    memset(enrollName,0,sizeof(enrollName));

    strncpy(enrollName,name,31);

    enrollAdmin = admin;
}

/* ================= VERIFY ================= */

void sendVerify()
{
  uint8_t pkt[]={0xEF,0xAA,0x12,0x00,0x02,0x00,0x0A,0x1A};

  OMS.write(pkt,sizeof(pkt));

  verifying=true;
  verifyStart = millis();
}

/* ================= PARSER ================= */

enum ParserState{
  WAIT_H1,
  WAIT_H2,
  WAIT_MID,
  WAIT_L1,
  WAIT_L2,
  WAIT_PAYLOAD,
  WAIT_CS
};

ParserState state=WAIT_H1;

uint8_t mid;
uint16_t len;
uint8_t buf[256];
uint16_t idx=0;

/* ================= HANDLE PACKET ================= */

void processPacket()
{
  if(mid==MID_NOTE)
  {
    uint8_t nid = buf[0];

    if(nid == 0x01 && len >= 17)
    {
        uint16_t faceState = buf[1] | (buf[2] << 8);

        switch(faceState)
        {
            case 1:
                faceDetected = false;
                break;

            case 6:
                faceDetected = true;
                show_status_popup("MOVE CLOSER");
                break;

            case 7:
                faceDetected = true;
                show_status_popup("MOVE FARTHER");
                break;

            case 11:
                faceDetected = true;
                show_status_popup("LOOK CENTER");
                break;

            default:
                faceDetected = true;
                show_status_popup("FACE DETECTED");
        }
    }

    return;
  }

  if(mid==MID_REPLY)
  {
    if(len<2) return;

    uint8_t repliedMsg=buf[0];
    uint8_t result=buf[1];

    /* ================= ENROLL ================= */

    if(repliedMsg==MID_ENROLL)
    {
        if(result!=0x00)
        {
            enrolling=false;

            close_pose_popup();
            show_popup_fail("ENROLL FAILED");

            return;
        }

        enrollStep++;

        update_pose();

        if(enrollStep>=5)
        {
            enrolling=false;

            close_pose_popup();
            show_popup_success("ENROLL SUCCESS");

            return;
        }

        delay(800);

        sendEnroll(poseList[enrollStep]);

        return;
    }

    /* ================= VERIFY ================= */

    if(repliedMsg==MID_VERIFY)
    {
      verifying=false;
      verifyCooldown = millis() + 1500;

      if(result!=0x00)
      {
        if(faceDetected)
          {
              ui_verify_fail();  // ACCESS DENIED
          }
          return;
      }

      uint16_t userID=(buf[2]<<8)|buf[3];

      char name[33];
      memset(name,0,sizeof(name));

      memcpy(name,&buf[4],32);

      ui_verify_success(name);

      Serial.print("ACCESS GRANTED: ");
      Serial.println(name);

      return;
    }
  }
}

/* ================= UART READER ================= */

void readOMS()
{
  while(OMS.available())
  {
    uint8_t b=OMS.read();

    switch(state)
    {
      case WAIT_H1:
        if(b==HDR1) state=WAIT_H2;
        break;

      case WAIT_H2:
        if(b==HDR2) state=WAIT_MID;
        else state=WAIT_H1;
        break;

      case WAIT_MID:
        mid=b;
        state=WAIT_L1;
        break;

      case WAIT_L1:
        len=b<<8;
        state=WAIT_L2;
        break;

      case WAIT_L2:
        len|=b;
        idx=0;
        state=WAIT_PAYLOAD;
        break;

      case WAIT_PAYLOAD:
        buf[idx++]=b;
        if(idx>=len) state=WAIT_CS;
        break;

      case WAIT_CS:
        processPacket();

        state=WAIT_H1;
        idx=0;
        len=0;
        break;
    }
  }
}

/* ================= PUBLIC FUNCTIONS ================= */

void oms_init()
{
  OMS.begin(115200,SERIAL_8N1,RXD2,TXD2);

  delay(1500);

  sendReset();
}

void oms_loop()
{
  readOMS();
  /* verify timeout protection */

  if(verifying && millis() - verifyStart > 2000)
  {
      verifying = false;
  }
}

/* ================= START ENROLL ================= */

void oms_startEnroll()
{
  enrolling=true;
  enrollStep=0;

  sendEnroll(poseList[0]);
}

/* ================= START VERIFY ================= */

void oms_startVerify()
{
  if(verifying) return;

  sendVerify();
}