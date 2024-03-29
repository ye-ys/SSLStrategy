#include "BallSpeedModel.h"
#include "DefendUtils.h"
#include "DefPos2015.h"
#include "DribbleStatus.h"
#include "GDebugEngine.h"
#include "Goalie2015Hefei.h"
#include "KickStatus.h"
#include "PlayInterface.h"
#include "RobotSensor.h"
#include "TaskMediator.h"
#include "VisionModule.h"
#include "WorldModel.h"
#include "param.h"
#include "skill/Factory.h"
#include "utils.h"

#include "tinyxml/ParamReader.h"

#include <cmath>

using namespace Param;
using namespace Utils;
using namespace std;

namespace {
  const bool DEBUG_INFO_FLAG = true;

  const double DANGER_FACTOR         = 2.5;
  const double PENALTY_BUFFER        = 15.0;
  const double DEFEND_PENALTY_BUFFER = 25.0;

  const CGeoPoint LEFT_GOAL_POST          = CGeoPoint(-Field::PITCH_LENGTH/2, -Field::GOAL_WIDTH/2);
  const CGeoPoint RIGHT_GOAL_POST         = CGeoPoint(-Field::PITCH_LENGTH/2, Field::GOAL_WIDTH/2);
  const CGeoPoint GOAL_CENTER             = CGeoPoint(-Field::PITCH_LENGTH/2, 0);
  const CGeoPoint LEFT_GOAL_CENTER_FRONT  = CGeoPoint(-Field::PITCH_LENGTH/2 + 50, -10);
  const CGeoPoint RIGHT_GOAL_CENTER_FRONT = CGeoPoint(-Field::PITCH_LENGTH/2 + 50, 10);
  const CGeoPoint LEFT_CORNER             = CGeoPoint(-Field::PITCH_LENGTH/2, -Field::PITCH_WIDTH/2);
  const CGeoPoint RIGHT_CORNER            = CGeoPoint(-Field::PITCH_LENGTH/2, Field::PITCH_WIDTH/2);
  const CGeoPoint LEFT_PENALTY_END        = CGeoPoint(-Field::PITCH_LENGTH/2, -Field::PENALTY_AREA_WIDTH/2);
  const CGeoPoint RIGHT_PENALTY_END       = CGeoPoint(-Field::PITCH_LENGTH/2, Field::PENALTY_AREA_WIDTH/2);

  int GOALIE_FILL_IN_MODE;
}

CGoalie2015Hefei::CGoalie2015Hefei() {
  clearBall = false;
  fillIn = NONE;

  /* 守门员上前补位模式 0为不上前 */
  //PARAM_READER->readParams();
  DECLARE_PARAM_READER_BEGIN(Defence)
    READ_PARAM(GOALIE_FILL_IN_MODE)
  DECLARE_PARAM_READER_END
}

void CGoalie2015Hefei::plan(const CVisionModule* pVision) {
  int goalieNum = task().executor;
	const PlayerVisionT& enemy = pVision->TheirPlayer(BestPlayer::Instance()->getTheirBestPlayer());
  const PlayerVisionT& me    = pVision->OurPlayer(goalieNum);
  const MobileVisionT& ball  = pVision->Ball();
  const CGeoPoint& normalGoaliePoint = DefPos2015::Instance()->getDefPos2015(pVision).getGoaliePos();
  TaskT myTask(task());
  myTask.player.pos.setX(normalGoaliePoint.x());
  myTask.player.pos.setY(normalGoaliePoint.y());
  myTask.player.flag |= PlayerStatus::QUICKLY;
  if (ball.Valid())
    myTask.player.angle = (ball.Pos() - me.Pos()).dir();
  else
    myTask.player.angle = (me.Pos() - GOAL_CENTER).dir();

  CGoalie2015Hefei::FillInTarget fillInTarget = GOALIE_FILL_IN_MODE ? needToFillIn(pVision) : NONE;

  /**
   * CLEAR BALL
   * 优先状态 这一帧进入清球状态或者上一帧处于清球状态
   */
  if (needToClearBall(pVision) || clearBall == true) {
    fillIn = NONE;
    clearBall = true;
    if (DEBUG_INFO_FLAG)
	{
      GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(200, 0), "in clear ball");
	  cout<<"TaskMediator::Instance()->singleBack()="<<TaskMediator::Instance()->singleBack()<<endl;
	  cout<<"TaskMediator::Instance()->leftBack()="<<TaskMediator::Instance()->leftBack()<<endl;
	}

    /* 考虑清球角度 */
    double clearBallDir = CVector(ball.Pos() - GOAL_CENTER).dir();
    if (TaskMediator::Instance()->singleBack() == 0 && TaskMediator::Instance()->leftBack() != 0) {
      CGeoPoint leftpos = DefPos2015::Instance()->getDefPos2015(pVision).getLeftPos();
      CGeoPoint rightpos = DefPos2015::Instance()->getDefPos2015(pVision).getRightPos();
      double goal2leftdir  = (DefPos2015::Instance()->getDefPos2015(pVision).getLeftPos() - ball.Pos()).dir() - 0.3;
      double goal2rightdir = (DefPos2015::Instance()->getDefPos2015(pVision).getRightPos() - ball.Pos()).dir() + 0.3;
      if (AngleBetween(clearBallDir, goal2leftdir, goal2rightdir)) {
        if (goal2rightdir >= 0 && goal2leftdir >= 0) {
          clearBallDir = Normalize(goal2leftdir - Math::PI / 6);
        } else if (goal2leftdir <= 0 && goal2rightdir <= 0) {
          clearBallDir = Normalize(goal2rightdir + Math::PI / 6);
        } else {
          double toLeft  = Normalize(clearBallDir - goal2leftdir);
          double toRight = Normalize(goal2rightdir - clearBallDir);
          if (fabs(toLeft) > fabs(toRight))
            clearBallDir = Normalize(goal2rightdir + Math::PI / 6);
          else
            clearBallDir = Normalize(goal2leftdir - Math::PI / 6);
        }
      }
    } else if (TaskMediator::Instance()->singleBack() != 0) {
      double goal2singledir = (DefPos2015::Instance()->getDefPos2015(pVision).getSinglePos() - ball.Pos()).dir();
      if (fabs(goal2singledir - clearBallDir) < Math::PI / 18) {
        if (fabs(clearBallDir) > Math::PI / 10) {
          clearBallDir = -clearBallDir;
          if (fabs(clearBallDir) > Math::PI * 70 / 180) {
            if (clearBallDir < 0)
              clearBallDir = clearBallDir + Math::PI / 9;
            else
              clearBallDir = clearBallDir - Math::PI / 9;
          }
        } else {
          clearBallDir = clearBallDir + Math::PI * 7 / 18;
        }
      }
    } else {
      clearBallDir = CVector(ball.Pos() - GOAL_CENTER).dir();
    }

    /* 考虑对方射门车离球比较近的情况 */
    if (enemy.Pos().dist(ball.Pos()) < 100)
      clearBallDir =  CVector(ball.Pos() - GOAL_CENTER).dir();

    /* 修正角度范围 */
    if (clearBallDir >= Math::PI * 80 / 180) {
      clearBallDir = Math::PI * 80 / 180;
    } else if (clearBallDir <= -Math::PI * 80 / 180) {
      clearBallDir = -Math::PI * 80 / 180;
    }

    /* Stop状态下不清球 */
    if (WorldModel::Instance()->CurrentRefereeMsg() == "gameStop") {
      clearBall = false;
      myTask.player.pos = normalGoaliePoint;
      if (ball.Valid())
        myTask.player.angle = CVector(ball.Pos() - me.Pos()).dir();
      else
        myTask.player.angle = CVector(me.Pos() - GOAL_CENTER).dir();
      setSubTask(TaskFactoryV2::Instance()->GotoPosition(myTask));
    }

    if (clearBall == true) {
      if (DEBUG_INFO_FLAG)
        cout << pVision->Cycle() << " ";
      if (ball.Pos().dist(LEFT_GOAL_POST) < Vehicle::V2::PLAYER_SIZE*DANGER_FACTOR && ball.Pos().y() >= LEFT_GOAL_POST.y()
          && CVector(ball.Pos() - me.Pos()).dir() > 0) {
        setSubTask(PlayerRole::makeItGoto(goalieNum, LEFT_GOAL_CENTER_FRONT, (ball.Pos() - me.Pos()).dir(), CVector(0, 0), 0, 0));
        if (DEBUG_INFO_FLAG)
          cout << "left goal center front " << endl;
      } else if (ball.Pos().dist(RIGHT_GOAL_POST) < Vehicle::V2::PLAYER_SIZE*DANGER_FACTOR && ball.Pos().y() <= RIGHT_GOAL_POST.y()
          && CVector(ball.Pos() - me.Pos()).dir() < 0) {
        setSubTask(PlayerRole::makeItGoto(goalieNum, RIGHT_GOAL_CENTER_FRONT, (ball.Pos() - me.Pos()).dir(), CVector(0, 0), 0, 0));
        if (DEBUG_INFO_FLAG)
          cout << "right goal center front " << endl;
      } else if (isBallShotToGoal(pVision) && ball.Pos().x() > me.Pos().x() + Vehicle::V2::PLAYER_FRONT_TO_CENTER) {
        const double patrolDir = Normalize(ball.Vel().dir() + Math::PI/2);
        const CGeoLine patrolLine(me.Pos(), patrolDir);
        const CGeoLine ballVelLine(ball.Pos(), ball.Vel().dir());
        const CGeoLineLineIntersection intersection(patrolLine, ballVelLine);
        DribbleStatus::Instance()->setDribbleCommand(goalieNum, 2);
        if (DEBUG_INFO_FLAG)
          cout << "ball shot ";
        if (intersection.Intersectant() == true) {
          const CGeoPoint& point = intersection.IntersectPoint();
          setSubTask(PlayerRole::makeItGoto(goalieNum, point, clearBallDir, CVector(0, 0), 0, 0));
          if (DEBUG_INFO_FLAG)
            cout << "intercept " << endl;
        } else {
          setSubTask(PlayerRole::makeItNoneTrajGetBall(goalieNum, clearBallDir, CVector(0, 0), PlayerStatus::QUICKLY | PlayerStatus::DRIBBLING));
          if (DEBUG_INFO_FLAG)
            cout << "get ball " << endl;
        }
      } else {
        DribbleStatus::Instance()->setDribbleCommand(goalieNum, 2);
        setSubTask(PlayerRole::makeItNoneTrajGetBall(goalieNum, clearBallDir, CVector(0, 0), PlayerStatus::QUICKLY | PlayerStatus::DRIBBLING));
        if (DEBUG_INFO_FLAG)
          cout << "get ball" << endl;
      }
    }
  }

  /**
   * FILL IN
   * 这一帧判断需要补位 或者上一帧是补位状态
   * 同时不在清球状态
   */
  else if (GOALIE_FILL_IN_MODE && (fillInTarget != NONE || fillIn != NONE) && clearBall == false) {

    /* fillIn是上一帧的补位状态 */
    if (fillInTarget == NONE)
      fillInTarget = fillIn;
    fillIn = fillInTarget;

    switch (fillInTarget) {
      case (BALL) : {
        CGeoPoint defendPoint = getFillInBallPosition(pVision);
        myTask.player.pos.setX(defendPoint.x());
        myTask.player.pos.setY(defendPoint.y());
        myTask.player.angle = (ball.Pos() - GOAL_CENTER).dir();
        if (DEBUG_INFO_FLAG)
          GDebugEngine::Instance()->gui_debug_arc(defendPoint, 3, 0, 360, COLOR_RED);
      }
      if (DEBUG_INFO_FLAG)
        GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(100, 100), "BALL");
      break;

      case (OURBACKS) : {
        CGeoPoint defendPoint = getFillInBacksPosition(pVision);
        myTask.player.pos.setX(defendPoint.x());
        myTask.player.pos.setY(defendPoint.y());
        myTask.player.angle = (ball.Pos() - GOAL_CENTER).dir();
      }
      if (DEBUG_INFO_FLAG)
        GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(100, 100), "OURBACKS");
      break;

      case (NONE) :
      if (DEBUG_INFO_FLAG)
        GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(100, 100), "NONE");
      break;
    }

    /* 修正球在守门员后方时的跑位 */
    if (ball.Pos().dist(LEFT_GOAL_POST) < Vehicle::V2::PLAYER_SIZE*DANGER_FACTOR
        && ball.Pos().y() >= LEFT_GOAL_POST.y()
        && CVector(ball.Pos() - me.Pos()).dir() > 0) {
      fillIn = NONE;
      clearBall = true;
      setSubTask(PlayerRole::makeItGoto(goalieNum, LEFT_GOAL_CENTER_FRONT, (ball.Pos() - me.Pos()).dir(), CVector(0, 0), 0, 0));
    } else if (ball.Pos().dist(RIGHT_GOAL_POST) < Vehicle::V2::PLAYER_SIZE*DANGER_FACTOR
        && ball.Pos().y() <= RIGHT_GOAL_POST.y()
        && CVector(ball.Pos() - me.Pos()).dir() < 0) {
      fillIn = NONE;
      clearBall = true;
      setSubTask(PlayerRole::makeItGoto(goalieNum, RIGHT_GOAL_CENTER_FRONT, (ball.Pos() - me.Pos()).dir(), CVector(0, 0), 0, 0));
    } else if (ball.Pos().dist(GOAL_CENTER) < me.Pos().dist(GOAL_CENTER)) {
      fillIn = NONE;
      clearBall = true;
      DribbleStatus::Instance()->setDribbleCommand(goalieNum, 2);
      setSubTask(PlayerRole::makeItNoneTrajGetBall(goalieNum, myTask.player.angle, CVector(0, 0), PlayerStatus::QUICKLY | PlayerStatus::DRIBBLING));
    } else {
      setSubTask(TaskFactoryV2::Instance()->GotoPosition(myTask));
    }
    if (DEBUG_INFO_FLAG)
      GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(200, 0), "in fill in");
  }

  /**
   * NORMAL
   * 正常跑点状态 最低优先级 跑DefPos2015算出来的点
   */
  else {
    fillIn = NONE;
    if (DEBUG_INFO_FLAG)
      GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(200, 0), "in normal goalie");
    setSubTask(TaskFactoryV2::Instance()->SmartGotoPosition(myTask));
  }

  /**
   * 清球状态跳出
   */
  if (ball.Valid() && InOurPenaltyArea(ball.RawPos(), PENALTY_BUFFER) == false)
    clearBall = false;

  /**
   * 补位状态跳出
   */
  if (clearBall == true) {
    fillIn = NONE;
  } else if (fillIn != NONE) {
    static int cycles = 0;
    if (InOurPenaltyArea(ball.RawPos(), Vehicle::V2::PLAYER_SIZE))
      cycles = 0;
    else
      ++cycles;
    if (cycles > 3) {
      fillIn = NONE;
      cycles = 0;
    }
    if (isBallShotToGoal(pVision))
      fillIn = NONE;
  }
  CStatedTask::plan(pVision);
}

CPlayerCommand* CGoalie2015Hefei::execute(const CVisionModule* pVision) {
  if (subTask())
    return subTask()->execute(pVision);
  return 0;
}

CGeoPoint CGoalie2015Hefei::getFillInBallPosition(const CVisionModule* pVision) {
  const MobileVisionT& ball = pVision->Ball();

  /**
   * 如果球速比较大 去禁区边界等球
   */
  if (ball.Vel().mod() > 180) {
    /* 考虑球速线和禁区的交点 */
    const CGeoLine ballVelLine(ball.Pos(), ball.Vel().dir());
    const CGeoPoint leftCircleCenter(-Field::PITCH_LENGTH/2, -Field::PENALTY_AREA_L/2);
    const CGeoPoint rightCircleCenter(-Field::PITCH_LENGTH/2, Field::PENALTY_AREA_L/2);
    const CGeoCirlce leftPenaltyCircle(leftCircleCenter, Field::PENALTY_AREA_R);
    const CGeoCirlce rightPenaltyCircle(rightCircleCenter, Field::PENALTY_AREA_R);
    const CGeoPoint leftPenaltySegmentEnd(-Field::PITCH_LENGTH/2 + Field::PENALTY_AREA_DEPTH, -Field::PENALTY_AREA_L/2);
    const CGeoPoint rightPenaltySegmentEnd(-Field::PITCH_LENGTH/2 + Field::PENALTY_AREA_DEPTH, Field::PENALTY_AREA_L/2);
    const CGeoLine middlePenaltySegment(leftPenaltySegmentEnd, rightPenaltySegmentEnd);

    const CGeoLineLineIntersection penaltyLineIntersection(ballVelLine, middlePenaltySegment);
    const CGeoLineCircleIntersection leftPenaltyIntersection(ballVelLine, leftPenaltyCircle);
    const CGeoLineCircleIntersection rightPenaltyIntersection(ballVelLine, rightPenaltyCircle);

    /* 球速线和禁区边界最多三个交点 */
    const CGeoPoint* points[3];
    points[0] = 0;
    points[1] = 0;
    points[2] = 0;

    /* 交点在中间直线上 */
    if (penaltyLineIntersection.Intersectant()) {
      const CGeoPoint& point = penaltyLineIntersection.IntersectPoint();
      if (point.y() <= rightPenaltySegmentEnd.y() && point.y() >= leftPenaltySegmentEnd.y()) {
        points[0] = &point;
      }
    }

    /* 交点在两段圆弧上 */
    if (leftPenaltyIntersection.intersectant() || rightPenaltyIntersection.intersectant()) {
      const CGeoPoint* pointL[2] = {0};
      const CGeoPoint* pointR[2] = {0};
      if (leftPenaltyIntersection.intersectant()) {
        pointL[0] = &leftPenaltyIntersection.point1();
        pointL[1] = &leftPenaltyIntersection.point2();
        if (pointL[0]->x() < leftCircleCenter.x() || pointL[0]->y() > leftCircleCenter.y()) pointL[0] = 0;
        if (pointL[1]->x() < leftCircleCenter.x() || pointL[1]->y() > leftCircleCenter.y()) pointL[1] = 0;
      }
      if (rightPenaltyIntersection.intersectant()) {
        pointR[0] = &rightPenaltyIntersection.point1();
        pointR[1] = &rightPenaltyIntersection.point2();
        if (pointR[0]->x() < rightCircleCenter.x() || pointR[0]->y() < rightCircleCenter.y()) pointR[0] = 0;
        if (pointR[1]->x() < rightCircleCenter.x() || pointR[1]->y() < rightCircleCenter.y()) pointR[1] = 0;
      }
      if (pointL[0] != 0) {
        if      (points[0] == 0) points[0] = pointL[0];
        else                     points[1] = pointL[0];
      }
      if (pointL[1] != 0) {
        if      (points[0] == 0) points[0] = pointL[1];
        else if (points[1] == 0) points[1] = pointL[1];
        else                     points[2] = pointL[1];
      }
      if (pointR[0] != 0) {
        if      (points[0] == 0) points[0] = pointR[0];
        else if (points[1] == 0) points[1] = pointR[0];
        else if (points[2] == 0) points[2] = pointR[0];
      }
      if (pointR[1] != 0) {
        if      (points[0] == 0) points[0] = pointR[1];
        else if (points[1] == 0) points[1] = pointR[1];
        else if (points[2] == 0) points[2] = pointR[1];
      }
    }

    /* 综合考虑交点 找出最合适的 */
    const CGeoPoint* defendTarget = 0;
    if (ball.VelY() > 0) {
      if (points[0] != 0) {
        if (points[0]->y() > ball.RawPos().y()) {
          defendTarget = points[0];
          if (points[1] != 0) {
            if (points[1]->y() > defendTarget->y()) {
              defendTarget = points[1];
              if (points[2] != 0) {
                if (points[2]->y() > defendTarget->y())
                  defendTarget = points[2];
              }
            }
          }
        }
      }
    } else {
      if (points[0] != 0) {
        if (points[0]->y() < ball.RawPos().y()) {
          defendTarget = points[0];
          if (points[1] != 0) {
            if (points[1]->y() < defendTarget->y()) {
              defendTarget = points[1];
              if (points[2] != 0) {
                if (points[2]->y() < defendTarget->y())
                  defendTarget = points[2];
              }
            }
          }
        }
      }
    }
    if (defendTarget != 0) {
      /* 防角平分线 */
      double leftPostToBallDir = (*defendTarget - LEFT_GOAL_POST).dir();
      double rightPostToBallDir = (*defendTarget - RIGHT_GOAL_POST).dir();
      double defendDir = Normalize((leftPostToBallDir + rightPostToBallDir) / 2 + Math::PI);

      /* 角度修正 */
      if (defendDir > 30 / 180 * Math::PI)
        defendDir -= 10 / 180 * Math::PI;
      else if (defendDir < -30 / 180 * Math::PI)
        defendDir += 10 / 180 * Math::PI;

      if (DEBUG_INFO_FLAG) {
        const CGeoPoint tmp = *defendTarget + Polar2Vector(100, defendDir + Math::PI);
        GDebugEngine::Instance()->gui_debug_line(*defendTarget, tmp, COLOR_YELLOW);
      }
      return *defendTarget + Polar2Vector(Vehicle::V2::PLAYER_SIZE*2.5, defendDir);
    }
  }

  /**
   * 如果速度比较小
   * 或者没有球速线和禁区边界没有能用的交点
   * 上去跟球 防角平分线
   */
  double leftPostToBallDir = (ball.Pos() - LEFT_GOAL_POST).dir();
  double rightPostToBallDir = (ball.Pos() - RIGHT_GOAL_POST).dir();
  double defendDir = Normalize((leftPostToBallDir + rightPostToBallDir) / 2 + Math::PI);

  /* 角度修正 */
  if (defendDir > 30 / 180 * Math::PI)
    defendDir -= 10 / 180 * Math::PI;
  else if (defendDir < -30 / 180 * Math::PI)
    defendDir += 10 / 180 * Math::PI;
  CGeoPoint defendPoint = ball.Pos() + Polar2Vector(Vehicle::V2::PLAYER_SIZE*2.5, defendDir);

  /* 防止守门员撞门柱 */
  const CGeoPoint* POST = 0;
  if (defendPoint.dist(LEFT_GOAL_POST) < Vehicle::V2::PLAYER_SIZE)
    POST = &LEFT_GOAL_POST;
  else if (defendPoint.dist(RIGHT_GOAL_POST) < Vehicle::V2::PLAYER_SIZE)
    POST = &RIGHT_GOAL_POST;
  if (POST != 0) {
    CGeoLine meToPostLine(defendPoint, *POST);
    CGeoCirlce postCircle(*POST, Vehicle::V2::PLAYER_SIZE);
    CGeoLineCircleIntersection intersection(meToPostLine, postCircle);
    const CGeoPoint& point1 = intersection.point1();
    const CGeoPoint& point2 = intersection.point2();
    if (point1.y() > -Field::PITCH_LENGTH / 2) {
      defendPoint.setX(point1.x());
      defendPoint.setY(point1.y());
    } else if (point2.y() > -Field::PITCH_LENGTH / 2) {
      defendPoint.setX(point2.x());
      defendPoint.setY(point2.y());
    }
  }
  return defendPoint;
}

CGeoPoint CGoalie2015Hefei::getFillInBacksPosition(const CVisionModule* pVision) {
  const MobileVisionT& ball = pVision->Ball();
  const CGeoLine ballToGoalLine(ball.RawPos(), GOAL_CENTER);
  const CGeoPoint leftPenaltySegmentEnd(-Field::PITCH_LENGTH / 2 + Field::PENALTY_AREA_DEPTH, -Field::PENALTY_AREA_L / 2);
  const CGeoPoint rightPenaltySegmentEnd(-Field::PITCH_LENGTH / 2 + Field::PENALTY_AREA_DEPTH, Field::PENALTY_AREA_L / 2);
  const CGeoLine middlePenaltySegment(leftPenaltySegmentEnd, rightPenaltySegmentEnd);
  const CGeoLineLineIntersection penaltyLineIntersection(ballToGoalLine, middlePenaltySegment);
  if (penaltyLineIntersection.Intersectant() == true
      && penaltyLineIntersection.IntersectPoint().y() > leftPenaltySegmentEnd.y()
      && penaltyLineIntersection.IntersectPoint().y() < rightPenaltySegmentEnd.y()) {
    const CGeoPoint& point = penaltyLineIntersection.IntersectPoint();
    double defendDir = (GOAL_CENTER - point).dir();
    return point + Polar2Vector(Vehicle::V2::PLAYER_SIZE*2, defendDir);
  } else {
    const CGeoPoint& point = penaltyLineIntersection.Intersectant() ? penaltyLineIntersection.IntersectPoint() : ball.RawPos();
    if (point.y() < 0) {  // 和左边圆弧相交
      const CGeoPoint leftCircleCenter(-Field::PITCH_LENGTH/2, -Field::PENALTY_AREA_L/2);
      const CGeoCirlce leftPenaltyCircle(leftCircleCenter, Field::PENALTY_AREA_R);
      const CGeoLineCircleIntersection leftPenaltyIntersection(ballToGoalLine, leftPenaltyCircle);
      const CGeoPoint& point1 = leftPenaltyIntersection.point1();
      const CGeoPoint& point2 = leftPenaltyIntersection.point2();
      if (point1.y() < leftCircleCenter.y()) {
        double defendDir = (GOAL_CENTER - point1).dir();
        return point1 + Polar2Vector(Vehicle::V2::PLAYER_FRONT_TO_CENTER, defendDir);
      } else {
        double defendDir = (GOAL_CENTER - point2).dir();
        return point2 + Polar2Vector(Vehicle::V2::PLAYER_FRONT_TO_CENTER, defendDir);
      }
    } else {  // 和右边圆弧相交
      const CGeoPoint rightCircleCenter(-Field::PITCH_LENGTH/2, Field::PENALTY_AREA_L/2);
      const CGeoCirlce rightPenaltyCircle(rightCircleCenter, Field::PENALTY_AREA_R);
      const CGeoLineCircleIntersection rightPenaltyIntersection(ballToGoalLine, rightPenaltyCircle);
      const CGeoPoint& point1 = rightPenaltyIntersection.point1();
      const CGeoPoint& point2 = rightPenaltyIntersection.point2();
      if (point1.y() > rightCircleCenter.y()) {
        double defendDir = (GOAL_CENTER - point1).dir();
        return point1 + Polar2Vector(Vehicle::V2::PLAYER_FRONT_TO_CENTER, defendDir);
      } else {
        double defendDir = (GOAL_CENTER - point2).dir();
        return point2 + Polar2Vector(Vehicle::V2::PLAYER_FRONT_TO_CENTER, defendDir);
      }
    }
  }
}

CGoalie2015Hefei::FillInTarget CGoalie2015Hefei::needToFillIn(const CVisionModule* pVision) {
  int goalie = PlayInterface::Instance()->getNumbByRealIndex(TaskMediator::Instance()->goalie());
  const MobileVisionT& ball = pVision->Ball();
  const CGeoPoint& rawBallPos = ball.RawPos();

  /**
   * 1. 考虑几种不补位的情况：
   *   - Stop状态，球还没开出来不补位
   *   - 射门了不补位
   *   - 球速过大不补位
   *   - 球离禁区远不补位
   *   - 球在禁区深处不补位
   */
  if (pVision->GetCurrentRefereeMsg() == "gameStop") {
    if (DEBUG_INFO_FLAG)
      GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(300, 0), "fillin: stop");
    return NONE;
  }
  if (ball.Valid() == true) {
    bool isBallShotToTarget = isBallShotToGoal(pVision);
    if (isBallShotToTarget == true && ball.Vel().mod() > 100) {
      if (DEBUG_INFO_FLAG)
        GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(300, 0), "fillin: ball shot");
      return NONE;
    }
  }
  if (ball.Vel().mod() > 600) {
    if (DEBUG_INFO_FLAG)
      GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(300, 0), "fillin: ball too fast");
    return NONE;
  }
  if (InOurPenaltyArea(rawBallPos, Vehicle::V2::PLAYER_SIZE) == false) {
    if (DEBUG_INFO_FLAG)
      GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(300, 0), "fillin: ball too far");
    return NONE;
  }
  if (InOurPenaltyArea(rawBallPos, -Field::PENALTY_AREA_DEPTH/3*2)) {
    if (DEBUG_INFO_FLAG)
      GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(300, 0), "fillin: ball too near");
    return NONE;
  }

  /**
   * 2. 考虑球在所有后卫后面挤成一团的情况
   *    球必须在禁区外面
   */
  bool isBallBehindBacks = true;
  double ballToGoalDist = rawBallPos.dist(GOAL_CENTER);
  for (int i = 1; i <= Field::MAX_PLAYER; ++i) {
    if (i != goalie && pVision->OurPlayer(i).Pos().dist(rawBallPos) < ballToGoalDist) {
      isBallBehindBacks = false;
      break;
    }
  }
  if (isBallBehindBacks == true
      && pVision->Ball().Vel().mod() < 150
      && InOurPenaltyArea(rawBallPos, 1 == false))
    return OURBACKS;

  /**
   * 3. 考虑球横穿禁区的情况
   */
  return BALL;
}

bool CGoalie2015Hefei::needToClearBall(const CVisionModule* pVision) {
  bool result = false;
  const MobileVisionT& ball = pVision->Ball();
  int advancer = BestPlayer::Instance()->getOurBestPlayer(); 
  const PlayerVisionT enemy = pVision->TheirPlayer(BestPlayer::Instance()->getTheirBestPlayer());
  // 150 120
  if (((ball.Vel().mod() < 200 && InOurPenaltyArea(ball.Pos(), -30)) ||
    (ball.Vel().mod() < 50 && InOurPenaltyArea(ball.Pos(), PENALTY_BUFFER)) && ball.Valid()
    && !checkWeHaveHelper(pVision) && !InOurPenaltyArea(enemy.Pos(), DEFEND_PENALTY_BUFFER)) ||
    ((ball.Vel().mod() < 170 && InOurPenaltyArea(ball.Pos(), -30)) ||
    (ball.Vel().mod() < 40 && InOurPenaltyArea(ball.Pos(), PENALTY_BUFFER)) && ball.Valid()
    && !checkWeHaveHelper(pVision) && InOurPenaltyArea(enemy.Pos(), DEFEND_PENALTY_BUFFER))){
    result = true;
    if (DEBUG_INFO_FLAG)
      GDebugEngine::Instance()->gui_debug_msg(CGeoPoint(50,0),"need to clear ball");
  }
  return result;
}

bool CGoalie2015Hefei::checkWeHaveHelper(const CVisionModule* pVision) {
  bool result = false;
  int helper[Field::MAX_PLAYER+1]={0};
  int helpernum = 0;
  int bestnum = BestPlayer::Instance()->getOurBestPlayer();
  const MobileVisionT& ball = pVision->Ball();
  for (int i = 1;i<= Field::MAX_PLAYER;i++){
    if (pVision->OurPlayer(i).Pos().dist(ball.Pos())<30){
      helpernum ++;
      helper[helpernum] = i;
    }
  }
  if (InOurPenaltyArea(ball.Pos(),PENALTY_BUFFER) && !InOurPenaltyArea(ball.Pos(),0)){
    if (pVision->TheirPlayer(bestnum).X()<ball.X() && 
        abs((GOAL_CENTER - ball.Pos()).dir() - (GOAL_CENTER - pVision->TheirPlayer(bestnum).Pos()).dir())<Math::PI/4
        && helpernum>=3)
      result = true;
  }
  return result;
}

bool CGoalie2015Hefei::isBallShotToGoal(const CVisionModule* pVision) {
  const MobileVisionT& ball = pVision->Ball();
  const CGeoPoint& ballPos = ball.Pos();
  CGeoLine baseLine(LEFT_PENALTY_END, RIGHT_PENALTY_END);
  CGeoLine ballVelLine(ballPos, ball.Vel().dir());
  CGeoLineLineIntersection intersetion(baseLine, ballVelLine);
  const CGeoPoint& point = intersetion.IntersectPoint();
  bool isBallShotToTarget = false;
  if (ball.Vel().mod() > 30
      && ball.VelX() < 0
      && point.y() < RIGHT_PENALTY_END.y() + Vehicle::V2::PLAYER_SIZE
      && point.y() > LEFT_PENALTY_END.y() - Vehicle::V2::PLAYER_SIZE)
    isBallShotToTarget = true;
  return isBallShotToTarget;
}

int CGoalie2015Hefei::getTheirReceiverCount(const CVisionModule* pVision) { int count = 0;
  const MobileVisionT& ball = pVision->Ball();

  for (int i = 1; i <= Field::MAX_PLAYER; ++i) {
    const PlayerVisionT& enemy = pVision->TheirPlayer(i);
    double ballToEnemyDir = (enemy.Pos() - ball.Pos()).dir();
    double enemyDir = enemy.Dir();
    double diffDir  = fabs(Normalize(ballToEnemyDir - enemyDir));

    double blockDir;
    if (diffDir <= Math::PI * 100 / 180) // 对方车接不到球
      blockDir = DefendUtils::calcBlockAngle(ball.Pos(), enemy.Pos()) + Math::PI*3/180;
    else                             // 对方车能接到球
      blockDir = DefendUtils::calcBlockAngle(ball.Pos(), enemy.Pos()) + Math::PI*10/180;

    if (ballToEnemyDir + blockDir <= Math::PI && ballToEnemyDir - blockDir >= -Math::PI) {
      if (ball.Vel().dir() < ballToEnemyDir + blockDir && ball.Vel().dir() > ballToEnemyDir - blockDir)
        ++count;
    } else if (ballToEnemyDir - blockDir >= -Math::PI) {
      if (ball.Vel().dir() < Normalize(ballToEnemyDir + blockDir) || ball.Vel().dir() > ballToEnemyDir - blockDir)
        ++count;
    } else if (ballToEnemyDir + blockDir <= Math::PI) {
      if (ball.Vel().dir() > Normalize(ballToEnemyDir - blockDir) || ball.Vel().dir() < ballToEnemyDir + blockDir)
        ++count;
    } else {
      if (ball.Vel().dir() < Normalize(ballToEnemyDir + blockDir) || ball.Vel().dir() > Normalize(ballToEnemyDir - blockDir))
        ++count;
    }
  }

  return count;
}
