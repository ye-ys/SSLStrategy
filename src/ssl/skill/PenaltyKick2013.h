#ifndef _PENALTY_KICK_2013_H_
#define _PENALTY_KICK_2013_H_

#include <skill/PlayerTask.h>

class CPenaltyKick2013 : public CStatedTask
{
public:
	CPenaltyKick2013(void);
	~CPenaltyKick2013(void){}
	virtual void plan(const CVisionModule* pVision);
protected:
	virtual void toStream(std::ostream& os) const { os << "Penalty kick"; }
	void planPrepare(const CVisionModule* pVision);
	void planWaiting(const CVisionModule* pVision);
	void planRotate(const CVisionModule* pVision);
	void planKick(const CVisionModule* pVision);
	bool canShoot(const CVisionModule* pVision);
	void planStop(const CVisionModule* pVision);
private:
	int _lastCycle;
	int _theirGoalie;
	CGeoPoint _targetPoint;//左边还是右边的目标点
    int _targetside;
	int _waitingcount;
	int _round;
	int _stopNum;
	int _stopCount;
	int _lastState;
};
#endif