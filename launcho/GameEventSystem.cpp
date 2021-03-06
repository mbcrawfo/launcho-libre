#include "GameEventSystem.h"
#include "utility/Log.h"
#include "events/Event.h"
#include "events/InputEvents.h"
#include <cassert>
#include <algorithm>
#include <iterator>

const std::string GameEventSystem::TAG = "GameEventSystem";

GameEventSystem::GameEventSystem(std::shared_ptr<sf::Window> window)
: timer(),
  window(window),
  listeners(),
  queues(),
  activeQueue(0)
{
  assert(window != nullptr);
}

void GameEventSystem::initialize()
{
}

void GameEventSystem::update(const float maxMs)
{
  timer.start();
  Log::verbose(TAG, "Starting event processing, time budget %.2fms", maxMs);
  processWindowEvents();
  processQueue(maxMs);
  Log::verbose(TAG, "Total event time %.2fms", timer.elapsedMilliF());
}

void GameEventSystem::destroy()
{
}

EventCallbackID GameEventSystem::generateNextCallbackID()
{
  static EventCallbackID currentID = 0;
  return ++currentID;
}

bool GameEventSystem::addListener(EventID evtID, EventCallbackID callbackID,
                                  EventCallback fn)
{
  // check for duplicate listeners
  auto itr = listeners.find(evtID);
  if (itr != listeners.end())
  {
    auto itr2 = itr->second.find(callbackID);      
    if (itr2 != itr->second.end())
    {
      Log::warning(
        TAG,
        "Attempt to double register callbackID %08x (event type %08x)",
        callbackID, 
        evtID
        );
      return false;
    }
  }
  else
  {
    listeners[evtID].emplace(callbackID, fn);
    Log::verbose(
      TAG,
      "Added callbackID %08x (event type %08x)",
      callbackID,
      evtID
      );    
  }  
  return true;
}

bool GameEventSystem::removeListener(EventID evtID, EventCallbackID callbackID)
{
  auto itr = listeners.find(evtID);
  if (itr != listeners.end())
  {
    auto itr2 = itr->second.find(callbackID);
    if (itr2 != itr->second.end())
    {
      itr->second.erase(itr2);
      Log::verbose(
        TAG,
        "Removed callbackID %08x (event type %08x)",
        callbackID,
        evtID
        );
    }
    else
    {
      Log::warning(
        TAG,
        "Tried to remove callbackID %08x (event type %08x), not found",
        callbackID,
        evtID
        );
    }
  }
  else
  {
    Log::warning(
      TAG, 
      "Tried to remove callbackID %08x (event type %08x), no listeners found",
      callbackID,
      evtID
      );
  }

  return false;
}

void GameEventSystem::triggerEvent(StrongEventPtr evt) const
{
  assert(evt != nullptr);
  Log::verbose(
    TAG,
    "Triggering event type %08x (%s)",
    evt->getID(),
    evt->getNameC()
    );

  auto itr = listeners.find(evt->getID());
  if (itr != listeners.end())
  {
    for (auto listener : itr->second)
    {
      listener.second(evt);
    }
  }
}

void GameEventSystem::queueEvent(StrongEventPtr evt)
{
  assert(evt != nullptr);
  queues[activeQueue].push_back(evt);
  Log::verbose(
    TAG, 
    "Queued event type %08x (%s)",
    evt->getID(),
    evt->getNameC()
    );
}

bool GameEventSystem::abortEvent(const EventID id)
{
  auto itr = std::find_if(
    queues[activeQueue].begin(),
    queues[activeQueue].end(),
    [&](StrongEventPtr evt) {
      return evt->getID() == id;
    }
    );

  if (itr != queues[activeQueue].end())
  {
    queues[activeQueue].erase(itr);
    Log::verbose(TAG, "Event type %08x aborted", id);
    return true;
  }
  else
  {
    Log::verbose(TAG, "Tried to abort event type %08x, none found", id);
    return false;
  }
}

uint32_t GameEventSystem::abortAllEvents(const EventID id)
{
  uint32_t count = queues[activeQueue].size();

  std::remove_if(
    queues[activeQueue].begin(),
    queues[activeQueue].end(),
    [&](StrongEventPtr evt) {
      return evt->getID() == id;
    }
    );

  count -= queues[activeQueue].size();
  Log::verbose(TAG, "Aborted %08x events of type %08x", count, id);
  return count;
}

void GameEventSystem::processWindowEvents()
{
  sf::Event evt;

  float start = timer.elapsedMilliF();
  int count = 0;
  while (window->pollEvent(evt))
  {
    count++;
    if (evt.type == sf::Event::Closed)
    {
      window->close();
    }
    else if (evt.type == sf::Event::KeyPressed || 
             evt.type == sf::Event::KeyReleased)
    {
      dispatchKeyboardEvent(evt);
    }
  }

  Log::verbose(
    TAG,
    "Processed %d window events in %.2fms",
    count,
    timer.elapsedMilliF() - start
    );
}

void GameEventSystem::processQueue(const float maxMs)
{
  EventQueue& q = queues[activeQueue];
  // flip the queues so that any new events generated during event processing
  // are sent to the new queue
  activeQueue = (activeQueue + 1) & 1;

  float start = timer.elapsedMilliF();
  int count = 0;
  while (!q.empty())
  {
    StrongEventPtr evt = q.front();
    q.pop_front();
    triggerEvent(evt);
    count++;

    if (timer.elapsedMilliF() > maxMs)
    {
      Log::warning(
        TAG,
        "Event processing aborted after %.2fms, %u events remaining",
        timer.elapsedMilliF(),
        q.size()
        );

      // copy remaining events to the start of the current queue
      std::copy(q.begin(), q.end(), std::front_inserter(queues[activeQueue]));
      q.clear();
    }
  }

  Log::verbose(
    TAG,
    "Processed %d events in %.2fms", 
    count, 
    timer.elapsedMilliF() - start
    );
}

void GameEventSystem::dispatchKeyboardEvent(const sf::Event& evt)
{
  enum
  {
    UP = 0,
    DOWN = 1,
    LEFT = 2,
    RIGHT = 3,
    FIRE = 4
  };
  // tracks keys that are currently down
  static bool keyStates[5] = { false };

  assert(evt.type == sf::Event::KeyPressed || 
         evt.type == sf::Event::KeyReleased);
  
  switch (evt.key.code)
  {
  case sf::Keyboard::Up:
    if ((evt.type == sf::Event::KeyPressed) && !keyStates[UP])
    {
      keyStates[UP] = true;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::MoveUp, InputActionState::Start)
          )
        );
    }
    else if ((evt.type == sf::Event::KeyReleased) && keyStates[UP])
    {
      keyStates[UP] = false;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::MoveUp, InputActionState::Stop)
          )
        );
    }
    break;

  case sf::Keyboard::Down:
    if ((evt.type == sf::Event::KeyPressed) && !keyStates[DOWN])
    {
      keyStates[DOWN] = true;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::MoveDown, InputActionState::Start)
          )
        );
    }
    else if ((evt.type == sf::Event::KeyReleased) && keyStates[DOWN])
    {
      keyStates[DOWN] = false;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::MoveDown, InputActionState::Stop)
          )
        );
    }
    break;

  case sf::Keyboard::Left:
    if ((evt.type == sf::Event::KeyPressed) && !keyStates[LEFT])
    {
      keyStates[LEFT] = true;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::MoveLeft, InputActionState::Start)
          )
        );
    }
    else if ((evt.type == sf::Event::KeyReleased) && keyStates[LEFT])
    {
      keyStates[LEFT] = false;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::MoveLeft, InputActionState::Stop)
          )
        );
    }
    break;

  case sf::Keyboard::Right:
    if ((evt.type == sf::Event::KeyPressed) && !keyStates[RIGHT])
    {
      keyStates[RIGHT] = true;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::MoveRight, InputActionState::Start)
          )
        );
    }
    else if ((evt.type == sf::Event::KeyReleased) && keyStates[RIGHT])
    {
      keyStates[RIGHT] = false;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::MoveRight, InputActionState::Stop)
          )
        );
    }
    break;

  case sf::Keyboard::Space:
    if ((evt.type == sf::Event::KeyPressed) && !keyStates[FIRE])
    {
      keyStates[FIRE] = true;
      queueEvent(
        StrongEventPtr(
          new InputEvent(InputAction::Fire)
          )
        );
    }
    else if ((evt.type == sf::Event::KeyReleased) && keyStates[FIRE])
    {
      keyStates[FIRE] = false;
    }
    break;
  }
}
