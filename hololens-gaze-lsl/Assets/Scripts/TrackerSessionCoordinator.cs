using System;
using System.Collections.Generic;

namespace GazeLSL
{
    public sealed class TrackerSessionCoordinator<TTracker, TState>
        where TTracker : class
        where TState : class
    {
        public struct OpenAttempt
        {
            internal OpenAttempt(object owner, long generation, TTracker tracker)
            {
                Owner = owner;
                Generation = generation;
                Tracker = tracker;
            }

            internal object Owner { get; }
            public long Generation { get; }
            public TTracker Tracker { get; }
            public bool IsValid => Owner != null && Tracker != null;
        }

        public sealed class ReadLease : IDisposable
        {
            private TrackerSessionCoordinator<TTracker, TState> owner;
            private Session session;

            internal ReadLease(TrackerSessionCoordinator<TTracker, TState> owner, Session session)
            {
                this.owner = owner;
                this.session = session;
            }

            public TTracker Tracker => session.Tracker;
            public TState State => session.State;
            public long Generation => session.Generation;

            public void Dispose()
            {
                TrackerSessionCoordinator<TTracker, TState> currentOwner = owner;
                Session currentSession = session;
                owner = null;
                session = null;
                if (currentOwner != null)
                {
                    currentOwner.Release(currentSession);
                }
            }
        }

        internal sealed class Session
        {
            public long Generation;
            public TTracker Tracker;
            public TState State;
            public int Readers;
            public bool Retired;
            public bool Closed;
        }

        private readonly object gate = new object();
        private readonly object ownerToken = new object();
        private readonly Action<TTracker> closeTracker;
        private readonly Dictionary<long, TTracker> pending = new Dictionary<long, TTracker>();

        private long latestGeneration;
        private bool destroying;
        private Session active;

        public TrackerSessionCoordinator(Action<TTracker> closeTracker)
        {
            this.closeTracker = closeTracker ?? throw new ArgumentNullException(nameof(closeTracker));
        }

        public bool HasActiveSession
        {
            get
            {
                lock (gate)
                {
                    return active != null;
                }
            }
        }

        public OpenAttempt BeginOpen(TTracker tracker)
        {
            if (tracker == null)
            {
                throw new ArgumentNullException(nameof(tracker));
            }

            bool closeImmediately;
            OpenAttempt attempt;
            lock (gate)
            {
                closeImmediately = destroying;
                if (closeImmediately)
                {
                    attempt = default(OpenAttempt);
                }
                else
                {
                    long generation = ++latestGeneration;
                    pending.Add(generation, tracker);
                    attempt = new OpenAttempt(ownerToken, generation, tracker);
                }
            }

            if (closeImmediately)
            {
                closeTracker(tracker);
            }

            return attempt;
        }

        public bool TryActivate(OpenAttempt attempt, TState state)
        {
            if (state == null)
            {
                throw new ArgumentNullException(nameof(state));
            }

            TTracker rejectedTracker = null;
            TTracker retiredTracker = null;
            bool activated = false;

            lock (gate)
            {
                TTracker pendingTracker;
                bool knownAttempt = ReferenceEquals(attempt.Owner, ownerToken) &&
                    pending.TryGetValue(attempt.Generation, out pendingTracker) &&
                    ReferenceEquals(pendingTracker, attempt.Tracker);

                if (!knownAttempt)
                {
                    return false;
                }

                pending.Remove(attempt.Generation);
                if (destroying || attempt.Generation != latestGeneration)
                {
                    if (active == null || !ReferenceEquals(active.Tracker, attempt.Tracker))
                    {
                        rejectedTracker = attempt.Tracker;
                    }
                }
                else
                {
                    Session previous = active;
                    active = new Session
                    {
                        Generation = attempt.Generation,
                        Tracker = attempt.Tracker,
                        State = state
                    };
                    retiredTracker = RetireLocked(previous);
                    activated = true;
                }
            }

            CloseIfNeeded(retiredTracker);
            CloseIfNeeded(rejectedTracker);
            return activated;
        }

        public void Abandon(OpenAttempt attempt)
        {
            TTracker trackerToClose = null;
            lock (gate)
            {
                TTracker pendingTracker;
                if (!ReferenceEquals(attempt.Owner, ownerToken) ||
                    !pending.TryGetValue(attempt.Generation, out pendingTracker) ||
                    !ReferenceEquals(pendingTracker, attempt.Tracker))
                {
                    return;
                }

                pending.Remove(attempt.Generation);
                if (active == null || !ReferenceEquals(active.Tracker, attempt.Tracker))
                {
                    trackerToClose = attempt.Tracker;
                }
            }

            CloseIfNeeded(trackerToClose);
        }

        public bool TryAcquireRead(out ReadLease lease)
        {
            lock (gate)
            {
                if (active == null)
                {
                    lease = null;
                    return false;
                }

                active.Readers++;
                lease = new ReadLease(this, active);
                return true;
            }
        }

        public bool TryGetActiveState(out TState state)
        {
            lock (gate)
            {
                if (active == null)
                {
                    state = null;
                    return false;
                }

                state = active.State;
                return true;
            }
        }

        public bool Retire(TTracker tracker)
        {
            TTracker trackerToClose = null;
            bool retired = false;
            lock (gate)
            {
                if (active != null && ReferenceEquals(active.Tracker, tracker))
                {
                    Session previous = active;
                    active = null;
                    trackerToClose = RetireLocked(previous);
                    retired = true;
                }

                List<long> pendingGenerations = null;
                foreach (KeyValuePair<long, TTracker> item in pending)
                {
                    if (ReferenceEquals(item.Value, tracker))
                    {
                        if (pendingGenerations == null)
                        {
                            pendingGenerations = new List<long>();
                        }

                        pendingGenerations.Add(item.Key);
                    }
                }

                if (pendingGenerations != null)
                {
                    for (int i = 0; i < pendingGenerations.Count; i++)
                    {
                        pending.Remove(pendingGenerations[i]);
                    }

                    if (!retired)
                    {
                        trackerToClose = tracker;
                    }
                    retired = true;
                }
            }

            CloseIfNeeded(trackerToClose);
            return retired;
        }

        public void RetireActive()
        {
            TTracker trackerToClose;
            lock (gate)
            {
                Session previous = active;
                active = null;
                trackerToClose = RetireLocked(previous);
            }

            CloseIfNeeded(trackerToClose);
        }

        public void Destroy()
        {
            List<TTracker> pendingToClose;
            TTracker activeToClose;
            lock (gate)
            {
                if (destroying)
                {
                    return;
                }

                destroying = true;
                latestGeneration++;
                pendingToClose = new List<TTracker>(pending.Values);
                pending.Clear();

                Session previous = active;
                active = null;
                activeToClose = RetireLocked(previous);
            }

            CloseIfNeeded(activeToClose);
            for (int i = 0; i < pendingToClose.Count; i++)
            {
                CloseIfNeeded(pendingToClose[i]);
            }
        }

        private TTracker RetireLocked(Session session)
        {
            if (session == null || session.Retired)
            {
                return null;
            }

            session.Retired = true;
            if (session.Readers == 0)
            {
                session.Closed = true;
                return session.Tracker;
            }

            return null;
        }

        private void Release(Session session)
        {
            TTracker trackerToClose = null;
            lock (gate)
            {
                session.Readers--;
                if (session.Retired && session.Readers == 0 && !session.Closed)
                {
                    session.Closed = true;
                    trackerToClose = session.Tracker;
                }
            }

            CloseIfNeeded(trackerToClose);
        }

        private void CloseIfNeeded(TTracker tracker)
        {
            if (tracker != null)
            {
                closeTracker(tracker);
            }
        }
    }
}
