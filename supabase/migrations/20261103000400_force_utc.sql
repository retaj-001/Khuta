-- timestamptz is stored in UTC internally. Force new database sessions to emit
-- UTC as well; the dashboard converts recorded_at in the browser to ar-SA.
alter database postgres set timezone to 'UTC';
