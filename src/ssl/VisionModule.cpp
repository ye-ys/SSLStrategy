// Time-stamp: <14:27:11  10-13-2009>

#include <VisionModule.h>
#include <WorldModel.h>
#include <utils.h>
#include <playmode.h>
#include <tinyxml/ParamReader.h>
#include <DribbleStatus.h>
#include <RobotCapability.h>
#include <TaskMediator.h>
#include <KickStatus.h>
#include <BestPlayer.h>
#include <PlayInterface.h>
#include <fstream>
#include <RobotSensor.h>
#include <GDebugEngine.h>
#include <RobotsCollision.h>
#include <BallStatus.h>
#include "bayes/MatchState.h"
#include "defence/DefenceInfo.h"

///
/// @file   VisionModule.cpp
/// @author Yonghai Wu <liunian@zju.edu.cn>
/// @date   Tue Oct 13 14:26:36 2009
/// 
/// @brief  �Ӿ�Ԥ�����������˲���Ԥ��ȵ�
/// 
/// 
///
namespace {
	bool VERBOSE_MODE = false; // �����Ϣ
	bool IS_SIMULATION = false; // �Ƿ����
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// @fn	CVisionModule::CVisionModule(const COptionModule* pOption) : _pOption(pOption),
/// 	_timeCycle(0), _lastTimeCycle(0),  _ballKicked(false),
/// 	_ballInvalidMovedCycle(0),_chipkickDist(0.0),_kickerDir(0.0)
///
/// @brief	Constructor. 
///
/// @author	Yonghai Wu
/// @date	2009-10-13
///
/// @param	pOption	If non-null, the option. 
////////////////////////////////////////////////////////////////////////////////////////////////////

CVisionModule::CVisionModule() 
:_timeCycle(0), _lastTimeCycle(0), _ballKicked(false), _ourGoalie(0), _theirGoalie(0), _theirGoalieStrategyNum(0)
{	
	DECLARE_PARAM_READER_BEGIN(General)
	READ_PARAM(IS_SIMULATION)
	DECLARE_PARAM_READER_END
	
	WorldModel::Instance()->registerVision(this);
	for (int i=0; i < Param::Field::MAX_PLAYER; i++){
		//�ָ���ʼ���Է���Ԥ��Ϊ��ʶ��Ƕ���Ϣ
		_theirPlayerPredictor[i].setIsHasRotation(false);
	}

	RobotsCollisionDetector::Instance()->setVision(this);

	_theirPenaltyNum = 0;
	_ballVelChangeCouter = false;
}


void CVisionModule::registerOption(const COptionModule* pOption)
{
	_pOption = pOption;
	_gameState.init(pOption->MyColor());
}

CVisionModule::~CVisionModule(void)
{

}


void CVisionModule::SetRefRecvMsg(const RefRecvMsg msg)
{
	_refRecvMsg = msg;
}

void CVisionModule::SetNewVision(const VisualInfoT& vInfo)
{	
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////��Ҫ˵������ע��////////////////////////////
	//	�����ҷ�С��λ������࣬�Է���λ�����Ҳ࣬�ѿ������궨��<x y theta>	//
	//	����ʱ��������߻����ұߣ����ڲ��Դ�������ͳһ����Ϊ�ҷ�����볡	//
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	// Ĭ���ҷ����򳡵���ߣ��м�
	const bool invert = !(_pOption->MySide() == Param::Field::POS_SIDE_LEFT);

	if (_pOption->MyColor() == TEAM_BLUE) {
		_ourGoal = _refRecvMsg.blueGoal;
		_theirGoal = _refRecvMsg.yellowGoal;
		_ourGoalie = _refRecvMsg.blueGoalie;
		_theirGoalie = _refRecvMsg.yellowGoalie;
	} else{
		_ourGoal = _refRecvMsg.yellowGoal;
		_theirGoal = _refRecvMsg.blueGoal;
		_ourGoalie = _refRecvMsg.yellowGoalie;
		_theirGoalie = _refRecvMsg.blueGoalie;
	}
	/////////////////////////////////////////////////////////////////////////////
	/// @brief Step 0. ������ʼʱ����¼������Ϣ����������
	/////////////////////////////////////////////////////////////////////////////
	// ���µ�ǰ��ʱ������
	SetCycle(vInfo.cycle); 
	
	// ������ͼ�������е�ԭʼ����Ϣ
	_lastRawBallPos = _rawBallPos;
	_rawBallPos.SetValid(vInfo.ball.valid);
	// add by zhyaic 2013.6.3 ԭʼ��λ��Ӧ�÷���
	const int invertFactor = invert ? -1 : 1;
	_rawBallPos.SetPos(vInfo.ball.x * invertFactor,vInfo.ball.y * invertFactor);
	
	/////////////////////////////////////////////////////////////////////////////
    /// @brief Step 1. ������Ԥ��,Ҳ�������뵱ǰ��Ĺ۲�����˲�
    /////////////////////////////////////////////////////////////////////////////
    _ballPredictor.updateVision(vInfo.cycle, vInfo.ball, invert);
	
	//printf("%d\n", RobotCapFactory::Instance()->getRobotCap(0,2)->maxSpeed(0));
	/////////////////////////////////////////////////////////////////////////////
	/// @brief Step 2: �����ҷ��ͶԷ�������λ��Ԥ�⣬��ע�˲���
    /////////////////////////////////////////////////////////////////////////////
	//��#TODO����������ײ�������գ�
	RobotsCollisionDetector::Instance()->clearLastResult();
	const MobileVisionT& thisBall = _ballPredictor.getData(Cycle());	
	// ȷ����Ա����Ϣ�Ƿ���Ҫ����ȷ����ȷ���±���˫����Ա����Ϣ
	const bool player_invert = ( (_pOption->MyColor()==TEAM_YELLOW) && IS_SIMULATION);
	for (int i = 0; i < Param::Field::MAX_PLAYER; ++ i) {
		const VehicleInfoT& ourPlayer = vInfo.player[player_invert? i+Param::Field::MAX_PLAYER : i];
		const VehicleInfoT& theirPlayer = vInfo.player[player_invert? i : i+Param::Field::MAX_PLAYER];

		RobotsCollisionDetector::Instance()->setCheckNum(0, i+1);
		_ourPlayerPredictor[i].updateVision(vInfo.cycle, ourPlayer, thisBall, invert, vInfo.ourRobotIndex[i]);
		RobotsCollisionDetector::Instance()->setCheckNum(1, i+1);
		_theirPlayerPredictor[i].updateVision(vInfo.cycle, theirPlayer, thisBall, invert, vInfo.theirRobotIndex[i]);
	} 

	//��#TODO������˫����ǰ�ڳ��ϵ���Ա�������ҷ��ų��Ž����Է�ȫ��
	CheckBothSidePlayerNum();
	
    /////////////////////////////////////////////////////////////////////////////
	/// @brief Step 3: ����˫��ͨѶ�����ݣ������ʵ��ģʽ
    /////////////////////////////////////////////////////////////////////////////
	if (! IS_SIMULATION) {
		RobotSensor::Instance()->Update(this->Cycle());
	}

	/////////////////////////////////////////////////////////////////////////////
	/// @brief Step 4: ��ײ���
	/////////////////////////////////////////////////////////////////////////////
	// ��Ԥ��ģ����������˼򵥵���ײģ�⣬��������������
	// ������ʱ�򼺷��ͶԷ��ĳ���������ײ���������Ҫѡ��һ��
	// ʹ���ҷ��Ľ�����Դ���Ƚ����������ǿ��ܻᷢ����ʵ���ϱ��Է����ߣ�
	// ������Ϊ���ҷ����Ƶ����
	// ���ڵ������ǣ����������ʱ���������ҷ��Ľ����û������ʱ�������öԷ��Ľ����
	// ���Է�ֹ�����������
	// ��#TODO��
	_hasCollision = false;

	if (vInfo.ball.valid) { // ��������
		// ���ȿ��ǶԷ�
		for (int i = 0; i < Param::Field::MAX_PLAYER; ++ i) {
			if (_theirPlayerPredictor[i].collideWithBall()) {
				static int block_cycle = 0;
				if (!BallStatus::Instance()->getChipKickState()) {
					_hasCollision = true;
					_ballPredictor.setCollisionResult(Cycle(), _theirPlayerPredictor[i].ballCollidedResult());
					break;
				} else if (block_cycle++>15) {
					// �ڶ�������һ��ʱ����,����Ϊ�������������,��������ǵ���Խ���˳���
					_hasCollision = true;
					_ballPredictor.setCollisionResult(Cycle(), _theirPlayerPredictor[i].ballCollidedResult());
					BallStatus::Instance()->setChipKickState(false);
					block_cycle = 0;
					break;
				} else {
					//std::cout<<"have opp but ball is on the air"<<endl;
				}
			}
		}
		// Ȼ�����ҷ�
		for (int i = 0; i < Param::Field::MAX_PLAYER; ++ i) {
			if (_ourPlayerPredictor[i].collideWithBall()) {
				_hasCollision = true;
				_ballPredictor.setCollisionResult(Cycle(), _ourPlayerPredictor[i].ballCollidedResult());
				// ��ע�͵�������������ܻ���
				//_ballPredictor.getData(Cycle()).SetVel(0, 0); // �����������, ��Ȼ����̫��
				//cout<<"set: "<<_ballPredictor.getResult(_timeCycle).Vel().mod()<<endl;
				break;
			}
		}
	} else { // û�п�����
		// ���ȿ����ҷ�
		for (int i = 0; i < Param::Field::MAX_PLAYER; ++ i) {
			if (_ourPlayerPredictor[i].collideWithBall()) {
				_hasCollision = true;
				_ballPredictor.setCollisionResult(Cycle(), _ourPlayerPredictor[i].ballCollidedResult());
				//_ballPredictor.getData(Cycle()).SetVel(0, 0);  // �����������, ��Ȼ����̫��
				break;
			}
		}
		// Ȼ���ǶԷ�
		for (int i = 0; i < Param::Field::MAX_PLAYER; ++ i) {
			if (_theirPlayerPredictor[i].collideWithBall()) {
				static int block_cycle = 0;
				if (!BallStatus::Instance()->getChipKickState()) {
					_hasCollision = true;
					_ballPredictor.setCollisionResult(Cycle(), _theirPlayerPredictor[i].ballCollidedResult());
					break;
				} else if (block_cycle++>15) {
					_hasCollision = true;
					_ballPredictor.setCollisionResult(Cycle(), _theirPlayerPredictor[i].ballCollidedResult());
					BallStatus::Instance()->setChipKickState(false);
					block_cycle = 0;
					break;
				} else {
				}
			}
		}
	}
	
	judgeBallVelStable();

	/////////////////////////////////////////////////////////////////////////////
	/// @brief Step 5: �����������ز��ֵ��ϲ���Ϣ
	/////////////////////////////////////////////////////////////////////////////
	// ��#TODO�� ��״̬ģ�����״̬, �ⲿ�ֵ�ʱ����Ҫ��ϸ�µ�����
	BallStatus::Instance()->UpdateBallStatus(this);

	// ��#TODO�� ���µ���˫������������ܣ�ԽСԽ���������򣬱�Ҷ˹�˲�����ʹ��
	BestPlayer::Instance()->update(this); 
	
	// ��#TODO�� ���±�Ҷ˹�˲���������Ŀǰ����������ʽ
	MatchState::Instance()->update();

	DefenceInfo::Instance()->updateDefenceInfo(this);

	/////////////////////////////////////////////////////////////////////////////
	/// @brief Step 6: ���²��к���Ϣ ��������������ص��������
	/////////////////////////////////////////////////////////////////////////////
	CheckKickoffStatus(vInfo);
	int ref_mode = vInfo.mode;
	// ���²��к���Ϣ��һ�㵱�ҽ�������ģʽΪͣ��״̬ʱ���ж����Ƿ��߳�
	if (ref_mode >= PMStop && ref_mode< PMNone) {
		_gameState.transition(playModePair[ref_mode].ch, _ballKicked);
	}

	//���²��к���Ϣ
	UpdateRefereeMsg();

	// �������һ
	// һЩ�������״̬�£�����������⴦�����볡�صĳߴ�������
	// һ��Ҫ���򿴲��������Դ���
	if (!IS_SIMULATION) {
		if (_gameState.kickoff()) {				// ����ʱ
			if (!Ball().Valid() || Ball().Pos().dist(CGeoPoint(0,0)) > 20) {
				_ballPredictor.setPos(Cycle(),CGeoPoint(0,0));
				_ballPredictor.setVel(Cycle(),CVector(0,0));
			}
		}
		double penaltyX = Param::Field::PENALTY_MARK_X;

		if (_gameState.ourPenaltyKick()) {		// �ҷ�����ʱ
			if (!Ball().Valid() || Ball().Pos().dist(CGeoPoint(penaltyX,0)) > 20) {
				_ballPredictor.setPos(Cycle(),CGeoPoint(penaltyX,0));
				_ballPredictor.setVel(Cycle(),CVector(0,0));
			}
		}

		if (_gameState.theirPenaltyKick()) {	// �Է�����ʱ
			if (!Ball().Valid() || Ball().Pos().dist(CGeoPoint(-penaltyX,0)) > 20) {
				_ballPredictor.setPos(CGeoPoint(-penaltyX,0));
				_ballPredictor.setVel(Cycle(),CVector(0,0));
			}
		}
	}

	// �����������
	// ��������Ϣ������û������������λ������
	bool sensorBall = false;
	for (int i = 1; i <= Param::Field::MAX_PLAYER; i ++) {
		if (RobotSensor::Instance()->IsInfoValid(i) && RobotSensor::Instance()->IsInfraredOn(i)) {
			sensorBall = true;

			if (Ball().Valid()) {	// �򿴵����������źż����飬��ΪͨѶ���ܻᶪ
				if (Ball().Pos().dist(OurPlayer(i).Pos()) > Param::Vehicle::V2::PLAYER_SIZE + Param::Field::BALL_SIZE + 5) {
					RobotSensor::Instance()->ResetInraredOn(i);
				}
			} else {				// �򿴲��������ݺ����źž������λ��
				_ballPredictor.setPos(OurPlayer(i).Pos() + Utils::Polar2Vector(8.5,OurPlayer(i).Dir()));
				_ballPredictor.setVel(Cycle(),CVector(0,0));
			}

			break;
		}
	}

	// ��һ���Ǳ�֤PlayInterface��OurPlayer��Valid��ͬ��
	PlayInterface::Instance()->clearRealIndex();
	for (int i = 1; i <= Param::Field::MAX_PLAYER; ++i ) {
		PlayInterface::Instance()->setRealIndex(i, _ourPlayerPredictor[i-1].getResult(_timeCycle).realNum);
	}

	// ��googlebuf�л�öԷ����Ž�
	if(IS_SIMULATION){
		_theirGoalieStrategyNum = _theirGoalie;
	} else{
		for (int i = 1; i <= Param::Field::MAX_PLAYER; ++i) {
			if (_theirPlayerPredictor[i-1].getResult(_timeCycle).realNum == _theirGoalie) {
				_theirGoalieStrategyNum = i;
				break;
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////
	/// @brief Step 7: ������������ʾһЩ��Ҫ����Ϣ
	/////////////////////////////////////////////////////////////////////////////

	// �����ǰ���Ԥ��λ�� �� �������Ƿ��ڳ���
	GDebugEngine::Instance()->gui_debug_x(this->Ball().Pos(),COLOR_RED);
	// �����ǰ���Ԥ���ٶ� �� �����ֱֵ����ʾ
	GDebugEngine::Instance()->gui_debug_line(this->Ball().Pos(),this->Ball().Pos()+this->Ball().Vel(),COLOR_ORANGE);
	char velbuf[20];
	const double outballspeed = Ball().Vel().mod();
	sprintf(velbuf, "%f", outballspeed);
	if (outballspeed <= 800){
		GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(-320,150), velbuf, COLOR_BLACK);
	} else{
		GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(-320,150), velbuf, COLOR_RED);
	}

	// ����ҷ�С����Ԥ��λ�� : ���Գ��Ƿ��ڳ���
	for (int i = 1; i <= Param::Field::MAX_PLAYER; i ++) {
		GDebugEngine::Instance()->gui_debug_robot(OurPlayer(i).Pos(), OurPlayer(i).Dir());
	}
	// ����ҷ�С���ĺ����ź�
	if (sensorBall) {
		GDebugEngine::Instance()->gui_debug_arc(Ball().Pos(), 4*Param::Field::BALL_SIZE, 0, 360, COLOR_PURPLE);
		GDebugEngine::Instance()->gui_debug_arc(Ball().Pos(), 2*Param::Field::BALL_SIZE, 0, 360, COLOR_PURPLE);
	}
	return ;
}

void CVisionModule::CheckKickoffStatus(const VisualInfoT& info)
{
	if (_gameState.canEitherKickBall()) {	// ������ȥ����
		if (! _ballKicked ){	// ��û�б��ж�Ϊ�߳�
			if (gameState().ourRestart()) {
				const double OUR_BALL_KICKEDBUFFER = 5 + 3;	
				const CVector ballMoved = Ball().Pos() - _ballPosSinceNotKicked;
				if( ballMoved.mod2() > OUR_BALL_KICKEDBUFFER * OUR_BALL_KICKEDBUFFER ){
					_ballKicked = true;
				}
			} else {
				CBestPlayer::PlayerList theirList =  BestPlayer::Instance()->theirFastestPlayerToBallList();
				if (theirList.empty()) {
					_ballKicked = false;
				} else {
					const double THEIR_BALL_KICKED_BUFFER = 5 + 5;
					const CVector ballMoved = Ball().Pos() - _ballPosSinceNotKicked;
					if( ballMoved.mod2() > THEIR_BALL_KICKED_BUFFER * THEIR_BALL_KICKED_BUFFER ){
						_ballKicked = true;
					}
				}
			}
				
		}
	} else {					// ���Ѿ����ж�Ϊ�߳�
		_ballKicked = false;
		_ballPosSinceNotKicked = Ball().Pos();
	}

	return ;
}

void CVisionModule::CheckBothSidePlayerNum()
{
	// ͳ���ҷ�ʵ���ڳ��ϵ�С������
	_validNum = 0;
	int tempGoalieNum = PlayInterface::Instance()->getNumbByRealIndex(TaskMediator::Instance()->goalie());
	for (int i = 1; i <= Param::Field::MAX_PLAYER; i++) {		
		if (OurPlayer(i).Valid() && i != tempGoalieNum) {	
			_validNum++;
		}
	}
	_validNum = _validNum>(Param::Field::MAX_PLAYER - 1)?(Param::Field::MAX_PLAYER - 1):_validNum;

	// ͳ�ƶԷ�ʵ���ڳ��ϵ�С������
	_TheirValidNum = 0;
	for (int i = 1; i <= Param::Field::MAX_PLAYER; i++) {
		if (TheirPlayer(i).Valid())	{
			_TheirValidNum ++;
		}
	}
	_TheirValidNum = _TheirValidNum > Param::Field::MAX_PLAYER ? Param::Field::MAX_PLAYER : _TheirValidNum;

	return;
}

void CVisionModule::SetPlayerCommand(int num, const CPlayerCommand* pCmd)
{
	_ourPlayerPredictor[num-1].updateCommand(Cycle(), pCmd);
	CDribbleStatus* dribbleStatus = DribbleStatus::Instance();
	if( pCmd->dribble() ){
		dribbleStatus->setDribbleOn(pCmd->number(), Cycle(), Ball().Pos());
	}else{
		dribbleStatus->setDribbleOff(pCmd->number());
	}

	return ;
}

void CVisionModule::UpdateRefereeMsg()
{
	if (_lastRefereeMsg != _refereeMsg && _refereeMsg == "theirPenaltyKick") { // ��¼��ǰ�ǶԷ��ڼ�������
		_theirPenaltyNum++;
	}

	_lastRefereeMsg=_refereeMsg;
	if (! _gameState.canMove()) {
		_refereeMsg = "gameHalt";
	} else if( _gameState.gameOver()/* || _pVision->gameState().isOurTimeout() */){
		_refereeMsg = "gameOver";
	} else if( _gameState.isOurTimeout() ){
		_refereeMsg = "ourTimeout";
	} else if(!_gameState.allowedNearBall()){
		// �Է�����
		if(_gameState.theirIndirectKick()){
			_refereeMsg = "theirIndirectKick";
		} else if (_gameState.theirDirectKick()){
			_refereeMsg = "theirIndirectKick";
		} else if (_gameState.theirKickoff()){
			_refereeMsg = "theirKickOff";
		} else if (_gameState.theirPenaltyKick()){
			_refereeMsg = "theirPenaltyKick";
		} else{
			_refereeMsg = "gameStop";
		}
	} else if( _gameState.ourRestart()){
		if( _gameState.ourKickoff() ) {
			_refereeMsg = "ourKickOff";
		} else if(_gameState.penaltyKick()){
			_refereeMsg = "ourPenaltyKick";
		} else if(_gameState.ourIndirectKick()){
			_refereeMsg = "ourIndirectKick";
		} else if(_gameState.ourDirectKick()){
			_refereeMsg = "ourIndirectKick";
		}
	} else {
		_refereeMsg ="";
	}
}

const string CVisionModule::GetCurrentRefereeMsg() const{
	return _refereeMsg;
}

const string CVisionModule::GetLastRefereeMsg() const{
	return _lastRefereeMsg;
}


void CVisionModule::judgeBallVelStable(){
	if (ballVelValid())
	{
		if (fabs(Utils::Normalize(this->Ball().Vel().dir()-this->Ball(_lastTimeCycle-2).Vel().dir()))>Param::Math::PI*10/180 && this->Ball().Vel().mod()>20)
		{
			_ballVelChangeCouter++;
			_ballVelChangeCouter=min(_ballVelChangeCouter,4);
		}else{
			_ballVelChangeCouter--;
			_ballVelChangeCouter=max(_ballVelChangeCouter,0);
		}
		if (_ballVelChangeCouter>=3){
			_ballVelDirChanged=true;		
		}
		if (_ballVelChangeCouter==0)
		{
			_ballVelDirChanged=false;
		}
	}

}

bool CVisionModule::ballVelValid(){
	if (!_rawBallPos.Valid() || fabs(_rawBallPos.X() - _lastRawBallPos.X())<0.0000000001 )
	{
		return false;
	}else{
		return true;
	}
}