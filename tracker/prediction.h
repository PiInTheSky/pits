#define DEG2RAD (3.142 / 180)
#define SLOTSIZE 100
#define SLOTS (45000 / SLOTSIZE)
#define POLL_PERIOD 5

struct TPosition
{
	double LatitudeDelta;
	double LongitudeDelta;
};

void *PredictionLoop(void *some_void_ptr);
int GetSlot(int32_t Altitude);
