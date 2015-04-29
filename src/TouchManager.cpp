//
//  TouchManager.cpp
//
//
//  Created by Gal Sasson on 7/28/14.
//
//

#include "TouchManager.h"
#include "TouchEvent.h"

namespace ofxInterface {


TouchManager::TouchManager()
{
	velocitySmoothCoeff = 0.25f;
	touchVelocityDump = 0.6f;
	touchHasMovedThreshold = 50;
	bUpdateDispatch = false;
}

TouchManager::~TouchManager()
{
	if (scene == NULL) {
		return;
	}

	// do not send destroy event
	std::list<Node*> list;
	scene->getSubTreeList(list);
	for (Node* n: list)
	{
		n->bSendDestroy = false;
	}

}

void TouchManager::setup(Node *root, bool dispatchOnUpdate)
{
	scene = root;
	bUpdateDispatch = dispatchOnUpdate;
}

void TouchManager::update(float dt)
{

	// smooth velocity of touches
	map<int, TouchEvent*>::iterator it = touches.begin();
	for (;it!=touches.end(); it++)
	{
		(*it).second->velocitySmoothed *= touchVelocityDump;
	}

	if (bUpdateDispatch) {
		// dispatch all touches in the queue
		mutex.lock();
		vector<TouchAction> queue = touchQueue;
		touchQueue.clear();
		mutex.unlock();

		for (int i=0; i<queue.size(); i++) {
			TouchAction* t = &queue[i];
			switch (t->actionType) {
			case TOUCH_DOWN:
				dispatchTouchDown(t->id, t->pos);
				break;
			case TOUCH_MOVE:
				dispatchTouchMove(t->id, t->pos);
				break;
			case TOUCH_UP:
				dispatchTouchUp(t->id, t->pos);
				break;
			}
		}
	}
}


void TouchManager::draw(){
	ofMesh points;
	points.setMode(OF_PRIMITIVE_POINTS);
	map<int, TouchEvent*>::iterator it = touches.begin();
	for (;it!=touches.end(); it++){
		if(it->second){
			ofSetColor(255,32);
			ofDrawCircle(it->second->position, 20);
			points.addVertex(it->second->position);
			ofSetColor(255);
			ofDrawBitmapString(ofToString(it->first), it->second->position + ofVec2f(5, -3));
		}
	}
	points.draw();
}


void TouchManager::drawFingerStatus(int x, int y){
	map<int, TouchEvent*>::iterator it = touches.begin();
	float rectSizeW = 40;
	float rectSizeH = 20;
	float padding = 10;
	int xx = x;
	for (;it!=touches.end(); it++){
		if(it->second){
			string type;
			ofColor c;
			switch (it->second->type) {
				case TouchEvent::TYPE_UP: type= "UP"; c = ofColor::purple; break;
				case TouchEvent::TYPE_DOWN: type= "DOWN"; c = ofColor::yellow;break;
				case TouchEvent::TYPE_MOVE: type= "MOVE"; c = ofColor::blue; break;
				case TouchEvent::TYPE_EXIT: type= "EXIT"; c = ofColor::red; break;
				case TouchEvent::TYPE_ENTER: type= "ENTER"; c = ofColor::green; break;
				case TouchEvent::TYPE_CLICK: type= "CLICK"; c = ofColor::magenta; break;
			}
			ofSetColor(c);
			ofDrawRectangle(xx, y, rectSizeW, rectSizeH);
			ofSetColor(255);
			ofDrawBitmapString(ofToString(it->first), xx + 4, y + rectSizeH - 5 );
			float secondsAlive = (ofGetSystemTimeMicros() - it->second->timestamp) / 1000000.0f;
			ofDrawBitmapString(type + "\n" + ofToString(secondsAlive, 1), xx + 4, y + rectSizeH + 16);
			xx += padding + rectSizeW;
		}
	}
}

	

void TouchManager::touchDown(int id, const ofVec2f& p)
{
	if (bUpdateDispatch) {
		// queue touch to be dispatched in update
		TouchAction t(TOUCH_DOWN, id, p);
		mutex.lock();
		touchQueue.push_back(t);
		mutex.unlock();
	}
	else {
		dispatchTouchDown(id, p);
	}
}

void TouchManager::touchMove(int id, const ofVec2f& p)
{
	if (bUpdateDispatch) {
		// queue touch to be dispatched in update
		TouchAction t(TOUCH_MOVE, id, p);
		mutex.lock();
		touchQueue.push_back(t);
		mutex.unlock();
	}
	else {
		dispatchTouchMove(id, p);
	}
}

void TouchManager::touchUp(int id, const ofVec2f& p)
{
	if (bUpdateDispatch) {
		// queue touch to be dispatched in update
		TouchAction t(TOUCH_UP, id, p);
		mutex.lock();
		touchQueue.push_back(t);
		mutex.unlock();
	}
	else {
		dispatchTouchUp(id, p);
	}
}


void TouchManager::dispatchTouchDown(int id, const ofVec2f& p)
{
	TouchEvent *event = new TouchEvent();
	event->id = id;
	event->firstPosition = event->position = event->prevPosition = p;
	touches[id] = event;

	Node *receiver = getComponentUnder(p);
	if (receiver == NULL) {
		ofLogError("TouchManager","could not find node for touchDown");
		return;
	}

	event->setReceiver(receiver);
	event->lastSeenAbove = receiver;
    event->type = TouchEvent::TYPE_DOWN;

	// dispatch the event to the receiver
	receiver->touchDown(id, event);

	ofNotifyEvent(eventEveryTouchDown, *event, this);
}

void TouchManager::dispatchTouchMove(int id, const ofVec2f &p)
{
	// check if this touch exists
	map<int, TouchEvent*>::iterator eventIt = touches.find(id);
	if (eventIt == touches.end()) {
		ofLogWarning("TouchManager", "received touchMove for id (%d) without touchDown. ignoring this touch.", id);
		return;
	}

	// update touch info
	TouchEvent* event = eventIt->second;

	// discard if same position as the previous touch
	if (event->prevPosition == p) {
		return;
	}

	if ((event->firstPosition - p).length() > touchHasMovedThreshold)
	{
		event->bHasMoved = true;
	}
	event->prevPosition = event->position;
	event->prevVelocity = event->velocity;
	event->prevTimestamp = event->timestamp;
	event->position = p;
	event->timestamp = ofGetSystemTimeMicros();

	// there is a problem with calculating the velocity when delta time is too small
	// sometimes we get events with delta time of 0 or 1 ms
	if (event->timestamp - event->prevTimestamp > 5000) {
		event->velocity = (p - event->prevPosition)*1000000 / (event->timestamp-event->prevTimestamp);
		event->velocitySmoothed += (event->velocity-event->velocitySmoothed)*velocitySmoothCoeff;
	}


	if (event->receiver == NULL) {
		// the original component of this touch was destroyed
		// ignore this touch
		ofLogVerbose("TouchManager","ignoring touch id: %d",id);
		return;
	}

	// find node below touch point
	Node *node = getComponentUnder(p);
	if (node == NULL) {
		ofLogError("TouchManager","could not find node for touchMove");
		return;
	}

	// handle touch exit/enter events
	if (node != event->receiver &&
		event->lastSeenAbove == event->receiver) {
		// touch just went out of the origin receiver area,
		// send touchExit
		event->type = TouchEvent::TYPE_EXIT;
		event->receiver->touchExit(id, event);
	}
	else if (node == event->receiver &&
			 event->lastSeenAbove != event->receiver) {
		// touch just went into the origin receiver area,
		// send touchEnter
		event->type = TouchEvent::TYPE_ENTER;
		event->receiver->touchEnter(id, event);
	}

	event->lastSeenAbove = node;

	// send touchMove
	event->type = TouchEvent::TYPE_MOVE;
	event->receiver->touchMove(id, event);

	// notify listeners for every touch move
	ofNotifyEvent(eventEveryTouchMove, *event, this);
}

void TouchManager::dispatchTouchUp(int id, const ofVec2f &p)
{
	// check if this touch exists
	map<int, TouchEvent*>::iterator eventIt = touches.find(id);
	if (eventIt == touches.end()) {
		ofLogWarning("TouchManager", "received touchUp for id (%d) without touchDown. ignoring this touch.", id);
		return;
	}

	// update touch info
	TouchEvent* event = eventIt->second;
	event->prevPosition = event->position;
	event->prevVelocity = event->velocity;
	event->prevTimestamp = event->timestamp;
	event->position = p;
	event->timestamp = ofGetSystemTimeMicros();

	// there is a problem with calculating the velocity when delta time is too small
	// sometimes we get events with delta time of 0 or 1 ms
	if (event->timestamp - event->prevTimestamp > 5000) {
		event->velocity = (p - event->prevPosition)*1000000 / (event->timestamp-event->prevTimestamp);
		event->velocitySmoothed += (event->velocity-event->velocitySmoothed)*velocitySmoothCoeff;
	}

	Node* node = getComponentUnder(p);
	if (node == NULL) {
		ofLogError("TouchManager","could not find node for touchUp");
		endTouch(id);
		return;
	}

	if (event->receiver == NULL) {
		ofLogNotice("TouchManager","this touch does not belong to any component");
		endTouch(id);
		return;
	}

    event->type = TouchEvent::TYPE_UP;
	event->receiver->touchUp(id, event);

	ofNotifyEvent(eventEveryTouchUp, *event, this);

	endTouch(id);
}

void TouchManager::endTouch(int id)
{
	TouchEvent* te = touches[id];
	touches.erase(id);
	delete te;
}

Node* TouchManager::getComponentUnder(const ofVec2f &p)
{
	if (!scene) {
		return NULL;
	}

	std::list<Node*> compList = getAllComponentsUnder(p);
	if (compList.size() == 0) {
		return scene;
	}

	return *compList.begin();
}

std::list<Node*> TouchManager::getAllComponentsUnder(const ofVec2f &p)
{
	std::list<Node*> list;
	fillComponentsUnder(scene, p, list);
	list.sort(Node::topPlaneFirst);
	return list;
}

void TouchManager::fillComponentsUnder(Node* root, const ofVec2f &p, std::list<Node *> &list)
{
	if (root == NULL || !root->getEnabled()) {
		return;
	}

	if (root->contains(p)) {
		list.push_back(root);
	}

	for (int i=0; i<root->children.size(); i++) {
		fillComponentsUnder(root->children[i], p, list);
	}
}

} // namespace



