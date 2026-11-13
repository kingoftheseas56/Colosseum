import importlib.util
import pathlib
import socket
import unittest
from unittest.mock import Mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('browse_provider', ROOT / 'scripts/vinyl/provider.py')
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)
CHANNEL = 'UCbRSywya_rl8YS15Lo9ttsA'
PARAM = 'ggMPOg1uXzVLbmZnaWI4STNs'
VIDEO = 'KCMCPsUpD3w'


def endpoint(key, page):
    return {'browseEndpoint': {'browseId': key, 'browseEndpointContextSupportedConfigs': {
        'browseEndpointContextMusicConfig': {'pageType': 'MUSIC_PAGE_TYPE_' + page}}}}


def song_row(video=VIDEO, video_type='MUSIC_VIDEO_TYPE_ATV'):
    watch = {'watchEndpoint': {'videoId': video, 'watchEndpointMusicSupportedConfigs': {
        'watchEndpointMusicConfig': {'musicVideoType': video_type}}}}
    return {'musicResponsiveListItemRenderer': {
        'flexColumns': [{'musicResponsiveListItemFlexColumnRenderer': {'text': {'runs': runs}}} for runs in (
            [{'text': 'Source song', 'navigationEndpoint': watch}],
            [{'text': 'Source artist', 'navigationEndpoint': endpoint(CHANNEL, 'ARTIST')}])],
        'overlay': {'musicItemThumbnailOverlayRenderer': {'content': {
            'musicPlayButtonRenderer': {'playNavigationEndpoint': watch}}}}}}


def two_row(key='MPRE_source', page='ALBUM'):
    return {'musicTwoRowItemRenderer': {'title': {'runs': [
        {'text': 'Source album', 'navigationEndpoint': endpoint(key, page)}]},
        'navigationEndpoint': endpoint(key, page), 'subtitle': {'runs': [
            {'text': 'Album'}, {'text': ' · '},
            {'text': 'Source artist', 'navigationEndpoint': endpoint(CHANNEL, 'ARTIST')}]},
        'thumbnailRenderer': {'musicThumbnailRenderer': {'thumbnail': {'thumbnails': []}}}}}


def shelf(rows, token=''):
    value = {'contents': rows}
    if token:
        value['continuations'] = [{'nextContinuationData': {'continuation': token}}]
    return {'musicCarouselShelfRenderer': value}


class BrowseProviderTests(unittest.TestCase):
    def setUp(self):
        self.client = Mock()
        self.client.get_explore.return_value = {'trending': {'items': []}, 'new_releases': []}
        self.client.get_charts.return_value = {'countries': {'options': ['ZZ', 'IN', 'US']}, 'artists': []}

    def browse(self, kind='songs', **options):
        return provider.handle(dict(operation='browseCatalogue', value=kind, **options), client=self.client)

    def test_defaults_are_finite_and_register_playable_source_records_without_hydration(self):
        raw = {'videoId': VIDEO, 'title': 'Source song', 'artists': [{'id': CHANNEL, 'name': 'Source artist'}],
               'videoType': 'MUSIC_VIDEO_TYPE_ATV', 'duration_seconds': 190}
        self.client.get_explore.return_value['trending']['items'] = [raw, raw, dict(raw, videoType='MUSIC_VIDEO_TYPE_PODCAST_EPISODE')]
        page = self.browse()
        self.assertTrue(page['complete'])
        self.assertEqual(page['nextCursor'], {})
        self.assertEqual(len(page['items']), 1)
        item, record = page['items'][0], page['records'][0]
        self.assertEqual(item['albumId'], record['id'])
        self.assertEqual(item['trackId'], record['tracks'][0]['id'])
        self.assertEqual(item['trackIndex'], 0)
        self.assertEqual(record['tracks'][0]['videoId'], VIDEO)
        self.assertEqual(record['tracks'][0]['artistIds'], ['ytm:artist:' + CHANNEL])
        self.client.get_album.assert_not_called()
        self.client.get_artist.assert_not_called()

    def test_categories_preserve_returned_parameters_and_exclude_nonmusic_duplicates(self):
        self.client.get_mood_categories.return_value = {'Genres': [{'title': 'Rock', 'params': PARAM},
            {'title': 'Rock repeat', 'params': PARAM}], 'Podcasts': [{'title': 'Business', 'params': 'business'}]}
        page = provider.handle({'operation': 'browseCategories', 'value': ''}, client=self.client)
        self.assertEqual(page['source'], 'youtube-music')
        self.assertEqual([(v['title'], v['params'], v['group']) for v in page['categories']], [('Rock', PARAM, 'Genres')])
        self.assertTrue(page['categories'][0]['id'])

    def test_mixed_category_shelves_select_real_music_types_without_search(self):
        self.client._send_request.return_value = {'sections': [shelf([song_row(), song_row(),
            song_row('abcdefghijk', 'MUSIC_VIDEO_TYPE_PODCAST_EPISODE'), two_row(), two_row('VLplaylist', 'PLAYLIST')])]}
        songs = self.browse(category=PARAM)
        albums = self.browse('albums', category=PARAM)
        self.assertEqual(len(songs['items']), 1)
        self.assertEqual(len(albums['items']), 1)
        self.assertEqual(albums['records'][0]['browseId'], 'MPRE_source')
        self.assertEqual(songs['records'][0]['tracks'][0]['videoId'], VIDEO)
        self.assertTrue(songs['complete'])
        self.client.search.assert_not_called()
        self.client.get_mood_playlists.assert_not_called()
        self.client._send_request.assert_called_with('browse', {'browseId': 'FEmusic_moods_and_genres_category', 'params': PARAM})

    def test_real_continuation_is_bound_and_duplicate_or_repeated_pages_fail(self):
        self.client._send_request.return_value = {'sections': [shelf([song_row()], 'real_source_token')]}
        first = self.browse(category=PARAM)
        self.assertFalse(first['complete'])
        cursor = first['nextCursor']
        with self.assertRaises(provider.InvalidArgumentError):
            self.browse('albums', category=PARAM, cursor=cursor)
        with self.assertRaises(provider.InvalidArgumentError):
            self.browse(category='another_category', cursor=cursor)
        self.client._send_request.return_value = {'continuationContents': {'musicShelfContinuation': {
            'contents': [song_row('abcdefghijk')]}}}
        second = self.browse(category=PARAM, cursor=cursor)
        self.assertEqual(second['offset'], 1)
        self.assertTrue(second['complete'])
        self.client._send_request.assert_called_with('browse', {'continuation': 'real_source_token'})
        self.client._send_request.return_value = {'continuationContents': {'musicShelfContinuation': {
            'contents': [song_row()]}}}
        with self.assertRaises(provider.ProviderUnavailableError):
            self.browse(category=PARAM, cursor=cursor)
        self.client._send_request.return_value = {'continuationContents': {'musicShelfContinuation': {
            'contents': [song_row()], 'continuations': [{'nextContinuationData': {'continuation': 'real_source_token'}}]}}}
        with self.assertRaises(provider.ProviderUnavailableError):
            self.browse(category=PARAM, cursor=cursor)

    def test_artists_use_explicit_or_global_charts_with_no_invented_rank(self):
        self.client.get_charts.return_value['artists'] = [{'title': 'Source artist', 'browseId': CHANNEL, 'rank': None}]
        page = self.browse('artists', region='ZZ')
        self.client.get_charts.assert_called_once_with('ZZ')
        self.assertEqual(page['items'][0]['artistId'], 'ytm:artist:' + CHANNEL)
        self.assertNotIn('rank', page['items'][0])
        self.assertEqual([v['code'] for v in page['markets']], ['ZZ', 'IN', 'US'])
        self.assertEqual(provider.ytmusic_location_options('ZZ'), {})
        with self.assertRaises(provider.InvalidArgumentError):
            self.browse('artists', region='XX')

    def test_invalid_scope_options_and_cursors_are_rejected_before_network(self):
        for kind, options in [('videos', {}), ('artists', {'category': PARAM}), ('songs', {'region': 'IN'}),
            ('songs', {'category': 'https://invalid'}), ('songs', {'cursor': []}),
            ('songs', {'cursor': {'token': 'forged'}}), ('albums', {'genre': 'rock'}), ('artists', {}),
            ([], {}), ('songs', {'category': 'x' * 2049}), ('songs', {'refresh': 'true'})]:
            with self.subTest(kind=kind, options=options), self.assertRaises(provider.InvalidArgumentError):
                self.browse(kind, **options)
        self.assertEqual(self.client.mock_calls, [])

    def test_empty_and_malformed_provider_results_are_distinct(self):
        self.assertEqual(self.browse()['items'], [])
        self.client._send_request.return_value = {'sections': [shelf([two_row('VLplaylist', 'PLAYLIST')])]}
        self.assertEqual(self.browse(category=PARAM)['items'], [])
        self.client.get_explore.side_effect = socket.timeout('timed out')
        with self.assertRaises(provider.ProviderTimeoutError):
            self.browse()
        self.client.get_explore.side_effect = None
        self.client.get_explore.return_value = {'trending': None}
        with self.assertRaises(provider.ProviderUnavailableError):
            self.browse()
        self.client._send_request.return_value = {'unexpected': True}
        with self.assertRaises(provider.ProviderUnavailableError):
            self.browse(category=PARAM)

    def test_browse_cards_never_return_signed_media_as_artwork(self):
        self.client.get_explore.return_value['new_releases'] = [{'browseId': 'MPRE_source', 'title': 'Album',
            'thumbnails': [{'url': 'https://r1.googlevideo.com/videoplayback?sig=secret', 'width': 1000}]}]
        page = self.browse('albums')
        self.assertEqual(page['items'][0]['cover'], '')
        self.assertEqual(page['records'][0]['cover'], '')


if __name__ == '__main__':
    unittest.main()
